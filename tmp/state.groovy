/*
 * ============================================================================
 *  CROSS-RUN STATE (archive-based) + SVN CHANGELIST FORMATTER — reference
 * ============================================================================
 *
 *  Real library layout (split back out per namespace to deploy):
 *      vars/ci.groovy    -> ci.persistData(key, value)
 *                           ci.loadData(key, [from])
 *      vars/svn.groovy   -> svn.changelist(Map)         (public)
 *                           parseChangelist(log, width)  (@NonCPS helper, same file)
 *
 *  All steps used are sandbox-whitelisted: writeFile / readFile / fileExists /
 *  archiveArtifacts / copyArtifacts / bat. No rawBuild, no trusted-library
 *  accessors required. Requires the Copy Artifact plugin (copyArtifacts step).
 * ============================================================================
 */


/* ===========================================================================
 *  ci — cross-run state via build artifacts
 * ===========================================================================
 *
 *  Model: persist writes an artifact under a fixed state dir; load pulls it
 *  from a PRIOR build chosen by a selector. The selector is the whole gate —
 *  persist is unconditional, load decides which build's copy you get.
 *
 *  >>> HOW LOAD PICKS A BUILD <<<
 *  The Copy Artifact plugin's stock selectors each resolve to ONE build and copy
 *  from it — none of them means "the most recent build that actually has artifact
 *  X". (lastCompleted() points at the single newest build and returns null if
 *  that build didn't store, with no fallback.) So "last stored value, regardless
 *  of build result" is implemented here as a manual newest->oldest walk that
 *  stops at the first build carrying the artifact.
 *
 *  DEFAULT (onlySuccessful=false):
 *      Walk previousBuild newest->oldest, try copyArtifacts specific(n) at each,
 *      return the first hit. Independent of each build's result: a build that
 *      stored and then FAILED is still found; builds that failed BEFORE storing
 *      simply have no artifact and are skipped. No keep-forever marking needed.
 *      Normal case finds it on the first (immediately previous) build; the walk
 *      only goes deeper across a run of builds that never stored.
 *  onlySuccessful=true -> lastSuccessful():
 *      One selector, no walk — only the last GREEN build's copy. Use when you
 *      explicitly only want state from successful builds.
 */

// Persist a small string blob as a build artifact. Unconditional by design —
// load decides which build's copy to read; nothing special marked here.
void persistData(String key, String value) {
    String path = ".ci-state/${key}"                  // key = plain filename, no slashes
    writeFile file: path, text: (value ?: ''), encoding: 'UTF-8'
    archiveArtifacts artifacts: path,
                     onlyIfSuccessful: false,          // archive regardless of eventual build result
                     fingerprint: false,
                     allowEmptyArchive: false
}

// Load the blob a prior build stored under `key`, newest build first. Returns
// null when none within the lookback has it (first run / all recent builds
// failed before storing) -> caller handles bootstrap.
//   onlySuccessful=false (default): last stored value regardless of build result
//                                   (manual newest->oldest walk).
//   onlySuccessful=true           : last GREEN build only (single selector).
// maxLookback bounds the walk so a long run of non-storing builds can't scan all
// of history; if not found within it, treat as bootstrap.
String loadData(String key, boolean onlySuccessful = false, int maxLookback = 200) {
    String path = ".ci-state/${key}"

    if (onlySuccessful) {
        copyArtifacts projectName: env.JOB_NAME, selector: lastSuccessful(),
                      filter: path, target: '.', optional: true, fingerprintArtifacts: false
        return fileExists(path) ? readFile(path).trim() : null
    }

    // Walk builds newest->oldest; first one that actually has the artifact wins.
    // Build numbers are collected in a @NonCPS helper (plain ints are serializable,
    // so nothing non-serializable is held across the copyArtifacts step).
    for (int n : priorBuildNumbers(maxLookback)) {
        copyArtifacts projectName: env.JOB_NAME, selector: specific("${n}"),
                      filter: path, target: '.', optional: true, fingerprintArtifacts: false
        if (fileExists(path)) return readFile(path).trim()
    }
    return null
}

// @NonCPS: walk the previousBuild chain and return build numbers newest-first,
// capped at `limit`. Pure RunWrapper traversal, no pipeline steps -> keeping the
// (non-serializable) RunWrapper off the CPS stack, same pattern as parseLog.
// Excludes the current build (it hasn't stored yet this run).
@NonCPS
List priorBuildNumbers(int limit) {
    def nums = []
    def b = currentBuild.previousBuild
    while (b != null && nums.size() < limit) {
        nums << b.number
        b = b.previousBuild
    }
    return nums
}

/*  --- watermark usage (read at START, before any commit that moves HEAD) ---
 *
 *  def raw = ci.loadData('committedRev.txt')                 // default: last stored, any result
 *  //  or:  ci.loadData('committedRev.txt', true)            // onlySuccessful -> lastSuccessful()
 *  Integer last = raw?.isInteger() ? raw as int : null       // null => bootstrap
 *
 *  ... build ... commit binaries ...
 *
 *  // ONLY after a real, concluded commit (keep this call singular + last):
 *  ci.persistData('committedRev.txt', "${env.CUR_REV}")
 *
 *  Notes:
 *   - .ci-state/ lives at workspace root, OUTSIDE the wc/ checkout, so it is
 *     never swept into an svn add. With CheckoutUpdater wiping the workspace,
 *     loadData re-fetches into a clean tree each run — no stale carry-over.
 *   - Default load is result-independent: a build that stored then failed is
 *     still found on the next run (the walk doesn't care about build status).
 *     No keep-forever marking, so nothing accumulates and normal retention
 *     applies unchanged.
 *   - Retention: the walk only sees builds whose records still exist. If
 *     "discard old builds" rotates out every build back to the last store, the
 *     walk finds nothing -> re-bootstrap. Keep enough history (or widen
 *     retention) if the watermark must survive long gaps between commits.
 *   - Cost: normally one copyArtifacts (immediately previous build has it). The
 *     walk only goes deeper across a streak of builds that never stored, capped
 *     by maxLookback.
 */


/* ===========================================================================
 *  svn — changelist formatter:  "author - <first N chars of commit message>"
 * ===========================================================================
 */

// svn.changelist(Map) -> List<String>, one entry per revision in the range.
//   path    : repo path/URL to log (scope this to SOURCE, not the bins path,
//             or your own binary commits show up with the build user as author)
//   fromRev : lower bound (exclusive is handled by caller passing last+1)
//   toRev   : upper bound (default 'HEAD')
//   width   : summary truncation (default 50)
// Uses full (non -q) `svn log` because it needs the message body. The header's
// "N lines" count drives parsing, so multi-line messages are handled without
// relying on separator lines.
List changelist(Map a) {
    String path  = a.path
    String from  = "${a.fromRev}"
    String to    = a.get('toRev', 'HEAD')
    int    width = (a.get('width', 50)) as int

    if ((to != 'HEAD') && (from as int) > (to as int)) return []   // empty range guard

    String log = bat(script: "@svn log -r ${from}:${to} \"${path}\"",
                     returnStdout: true)
    return parseChangelist(log, width)
}

// @NonCPS: pure text parsing + regex, kept off the CPS stack (retaining a
// Matcher across a CPS boundary is the classic NotSerializableException).
// No pipeline steps inside — same reasoning as parseLog / triggeringUser.
// svn log entry shape:
//   r123 | author | 2026-08-12 10:00:00 +0200 (Wed, 12 Aug 2026) | 2 lines
//   <blank>
//   <message line 1>
//   <message line 2>
//   ------------------------------------------------------------------------
@NonCPS
List parseChangelist(String log, int width) {
    def out   = []
    def lines = log ? log.readLines() : []
    def hdr   = ~/^r(\d+)\s*\|\s*([^|]+?)\s*\|\s*[^|]+\|\s*(\d+)\s+lines?\s*$/
    int i = 0
    while (i < lines.size()) {
        def m = (lines[i] =~ hdr)
        if (m) {
            String author = m[0][2].trim()
            int    n      = m[0][3] as int
            int    start  = i + 2                       // skip header + the blank line after it
            String summary = ''
            for (int j = start; j < start + n && j < lines.size(); j++) {
                if (lines[j]?.trim()) { summary = lines[j].trim(); break }   // first non-empty msg line
            }
            if (!summary) summary = '(no message)'
            if (summary.length() > width) summary = summary.substring(0, width)
            out << "${author} - ${summary}"
            i = start + n                               // jump past this entry's message block
        } else {
            i++                                         // separator / stray line
        }
    }
    return out
}

/*  --- changelist usage ---
 *
 *  // authors+summaries for the range just built, for the commit message:
 *  def cl = svn.changelist(path: '^/trunk', fromRev: (last + 1), toRev: env.CUR_REV)
 *  String block = cl ? cl.collect { "  ${it}" }.join('\n') : '  (no source changes)'
 *  //  ->  fgrando - fix WCET overrun in scheduler init
 *  //      amaier   - bump external lib pin to r4471
 *
 *  Then drop `block` into releaseCommitMessage(...) alongside target/notes.
 *  Truncation is a hard cut to `width` chars (no ellipsis); add ' ...' in the
 *  caller if you want a visible marker.
 */