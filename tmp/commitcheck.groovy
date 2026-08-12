/*
 * ============================================================================
 *  RELEASE-COMMIT PIPELINE — consolidated reference
 * ============================================================================
 *
 *  This is a REFERENCE file. In the actual shared library these are three
 *  separate vars/ scripts, one per namespace, because each is invoked as
 *  <namespace>.<method>():
 *
 *      vars/ci.groovy       -> ci.triggeringUser(), ci.releaseCommitMessage(a)
 *      vars/svn.groovy      -> svn.stage(folder)
 *      vars/winenv.groovy   -> winenv.withNewFile(args, body)
 *
 *  Cross-namespace calls (e.g. svn.stage, winenv.withNewFile, ci.*) resolve
 *  because every vars script is a global in the pipeline binding.
 *
 *  Scope: Windows agents, SVN working copy, DO-178C-style atomic release
 *  revision. Assumptions baked in:
 *    - working copy checked out into  wc/
 *    - commit-message scratch file written at workspace ROOT (outside wc/)
 *    - monitored folders == the declared artifact set; changes outside them
 *      are intentionally neither gated nor committed.
 * ============================================================================
 */


/* ===========================================================================
 *  vars/ci.groovy
 * ===========================================================================
 */

// ci.triggeringUser()
// Resolves who/what triggered the build, plugin-free, sandbox-clean.
// getBuildCauses() returns sanitized plain Maps (whitelisted); do NOT use
// currentBuild.rawBuild.getCauses() — rawBuild is not whitelisted.
// Not @NonCPS: the .find{} closure calls no pipeline steps, so it is fine
// under the CPS transform, and the locals are plain ArrayList/HashMap.
String triggeringUser() {
    def causes = currentBuild.getBuildCauses()
    def uc = causes.find { it._class?.contains('UserIdCause') }
    if (uc) {
        return "${uc.userName} (${uc.userId})"
    }
    // upstream / timer / SCM trigger -> no UserIdCause
    return causes ? "automated: ${causes[0].shortDescription}" : 'unknown'
}

// ci.releaseCommitMessage(Map a)
// Assemble once in memory, write once (see winenv.withNewFile). Do NOT
// build this by repeated append-to-disk: one round-trip beats N.
// Expects: a.target  (the url@rev that was RELEASED)
//          a.notes   (user-typed release notes)
// Note: 'Build:' line is the durable, greppable identity; 'link:' is the
// convenient-but-perishable deep link. Keeping both is intentional.
String releaseCommitMessage(Map a) {
    [ "Release ${a.target}",
      "",
      (a.notes ?: '').trim(),
      "",
      "--",
      "Triggered-by: ${triggeringUser()}",          // same script -> unqualified
      "Build:        ${env.JOB_NAME} #${env.BUILD_NUMBER}",
      "link:         ${env.BUILD_URL}",
    ].join('\n') + '\n'
}


/* ===========================================================================
 *  vars/svn.groovy
 * ===========================================================================
 */

// svn.stage(String folder)
// Stages ONE folder for commit: adds unversioned (?), removes missing (!).
// Does NOT commit — the caller commits once over all folders (atomic revision).
// Returns [added:[...], deleted:[...]] for LOGGING/traceability only.
//   *** Do NOT use this return to decide whether to commit. ***
//   Rebuilt-in-place binaries show as 'M', not '?', so the return can be
//   empty on a real changeset. The commit gate is a folder-scoped
//   `svn status` in the Jenkinsfile (catches M/?/!/A).
//
// Non-verbose `svn status` layout: code in column 0, path from column 8.
// substring(8) avoids tokenizing -> safe for paths with spaces (umlauts).
// For exotic paths, `svn status --xml` removes the positional guesswork.
Map stage(String folder) {
    def raw = bat(script: "@svn status \"${folder}\"", returnStdout: true).trim()
    if (!raw) return [added: [], deleted: []]

    def added = [], deleted = []
    raw.readLines().each { line ->
        if (!line) return
        String code = line[0]
        String path = line.length() > 8 ? line.substring(8).trim() : ''
        if (!path) return
        if (code == '?') added   << path
        if (code == '!') deleted << path
    }

    // NOTE (parallel): svn-add.txt / svn-del.txt are shared scratch names.
    // Fine for a sequential folder loop. If you parallelize stage() calls,
    // derive per-folder names, e.g.:
    //   String key = folder.replaceAll(/\W/, '_')
    //   writeFile file: "svn-add-${key}.txt", ...
    if (added) {
        writeFile file: 'svn-add.txt', text: added.join('\n'), encoding: 'UTF-8'
        bat 'svn add --targets svn-add.txt'
    }
    if (deleted) {
        writeFile file: 'svn-del.txt', text: deleted.join('\n'), encoding: 'UTF-8'
        bat 'svn delete --targets svn-del.txt'
    }
    echo "svn.stage(${folder}): +${added.size()} / -${deleted.size()}"
    return [added: added, deleted: deleted]
}


/* ===========================================================================
 *  vars/winenv.groovy
 * ===========================================================================
 */

// winenv.withNewFile(Map args, Closure body)
// Create a file, run the body with its path, delete it in finally (fires
// even on throw). Windows-only as written (bat/del). All ops run agent-side
// via writeFile/fileExists/bat — a plain new File().delete() would run on
// the CONTROLLER and silently no-op (and is sandbox-blocked).
// Must stay CPS (calls pipeline steps + a CPS closure body) — no @NonCPS.
//   args.path      (required)
//   args.text      (default '')
//   args.encoding  (default 'UTF-8', no BOM)
def withNewFile(Map args, Closure body) {
    String path = args.path
    String text = args.get('text', '')
    String enc  = args.get('encoding', 'UTF-8')

    writeFile file: path, text: text, encoding: enc
    try {
        return body.call(path)                       // pass path in, propagate result out
    } finally {
        if (fileExists(path)) {
            bat(script: "del /f /q \"${path}\"", returnStatus: true)
        }
    }
}


def folders   = ['wc/comp-a/bins', 'wc/comp-b/bins', 'wc/comp-c/bins']
def stampFile = 'wc/RELEASE.txt'

// stamp genuinely changes every build (BUILD_NUMBER is unique per run)
def stamp = [
    "Release:      ${a.target}",
    "Build:        ${env.JOB_NAME} #${env.BUILD_NUMBER}",
    "Triggered-by: ${ci.triggeringUser()}",
].join('\n') + '\n'
writeFile file: stampFile, text: stamp, encoding: 'UTF-8'

// stage + gate + commit over folders AND the stamp (scope stays == gate)
def all     = folders + [stampFile]
all.each { svn.stage(it) }                       // stage() on a file path is fine:
                                                 //   first build '?' -> add; later 'M' -> rides the commit
def targets = all.collect { "\"${it}\"" }.join(' ')

def pending = bat(script: "@svn status ${targets}", returnStdout: true).trim()
if (!pending) { echo 'No changes — skipping commit.'; return }   // now effectively never fires

withNewFile(path: 'commit.msg', text: ci.releaseCommitMessage(a)) { f ->
    bat "svn commit ${targets} --file \"${f}\" --encoding UTF-8"
}


