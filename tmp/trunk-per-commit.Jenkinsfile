/*
 * ============================================================================
 *  PER-COMMIT TRUNK BUILD — declarative, state stored in the build description
 * ============================================================================
 *
 *  Two jobs:
 *    trunk-dispatcher   (this file, PART 1) — timer job; enumerates new trunk
 *                        revisions and fires one worker build per revision.
 *    trunk-per-commit   (this file, PART 2) — parameterized worker; checks out
 *                        one exact revision, builds, notifies that one author.
 *
 *  Why two jobs: with SVN polling and no hooks, a normal build jumps straight
 *  to HEAD and one build covers every revision since the last poll (several
 *  authors). To build per commit you must stop jumping to HEAD and enumerate
 *  the revisions yourself — that is what the dispatcher does.
 *
 *  STATE: the only thing persisted is one integer — the last revision handled.
 *  It lives in the dispatcher's BUILD DESCRIPTION, which the controller stores
 *  in build.xml. That is node-independent, survives workspace wipes and agent
 *  changes, and needs no plugin and no file outside the workspace.
 *  setDescription/description/previousBuild are all whitelisted on RunWrapper.
 *
 *  Deploy each PART as its own Jenkinsfile / job. They are together here only
 *  as one reference piece.
 * ============================================================================
 */


/* ===========================================================================
 *  PART 1 — trunk-dispatcher
 * ===========================================================================
 */

// @NonCPS: walk previousBuild + run the matcher off the CPS stack (retaining a
// Matcher across a CPS boundary is the classic NotSerializableException).
// Pure computation, no pipeline steps inside — same reasoning as triggeringUser.
@NonCPS
Integer readLastRev() {
    def b = currentBuild.previousBuild            // this job's history, controller-stored
    while (b != null) {
        String d = b.description
        if (d) {
            def m = (d =~ /lastRev=(\d+)/)
            if (m) return m[0][1] as int
        }
        b = b.previousBuild
    }
    return null                                   // no stamp anywhere -> bootstrap
}

pipeline {
    agent { label 'windows' }                     // any agent with svn — NOT pinned for state anymore
    triggers { cron('H/2 * * * *') }              // cron, not pollSCM: the dispatcher does its own enumeration
    options { disableConcurrentBuilds() }         // never two dispatchers racing the same lastRev

    environment {
        TRUNK = '^/trunk'
    }

    stages {
        stage('Dispatch new revisions') {
            steps {
                script {
                    int headRev = bat(script: "@svn info --show-item revision ${env.TRUNK}",
                                      returnStdout: true).trim() as int

                    Integer lastRev = readLastRev()

                    // --- bootstrap: adopt HEAD, build nothing (don't replay all history) ---
                    if (lastRev == null) {
                        currentBuild.description = "lastRev=${headRev}"
                        echo "Bootstrapped lastRev=${headRev}; no builds this run."
                        return
                    }

                    // --- nothing new: re-stamp so the description chain stays dense (1-hop walk) ---
                    if (headRev <= lastRev) {
                        currentBuild.description = "lastRev=${lastRev}"
                        echo 'No new revisions.'
                        return
                    }

                    // --- enumerate: -q => header lines only, no message body (no XML to parse
                    //     in-sandbox); path-scoped to ^/trunk => only trunk-touching revs, and
                    //     without -g no merged-in history is expanded ---
                    String log = bat(script: "@svn log -q -r ${lastRev + 1}:HEAD ${env.TRUNK}",
                                     returnStdout: true).trim()
                    def entries = parseLog(log).sort { it.rev }      // ascending == commit order

                    for (e in entries) {
                        build job: 'trunk-per-commit',
                              parameters: [ string(name: 'REV',    value: "${e.rev}"),
                                            string(name: 'AUTHOR', value: e.author) ],
                              wait: true, propagate: false           // sequential; a failing worker
                                                                     // won't abort the dispatch loop
                    }

                    // --- persist ONLY after triggering (replay-over-skip): if this throws mid-loop,
                    //     this build has no lastRev= stamp, so next run walks past it and re-dispatches
                    //     the unhandled revs rather than skipping them permanently ---
                    currentBuild.description = "lastRev=${headRev}"
                }
            }
        }
    }
}

// @NonCPS: keep the Matcher off the CPS stack. Parses `svn log -q` header lines:
//   r123 | fgrando | 2026-08-12 ... | N lines
@NonCPS
List parseLog(String log) {
    def out = []
    log.eachLine { line ->
        def m = (line =~ /^r(\d+)\s*\|\s*([^|]+?)\s*\|/)
        if (m) out << [rev: m[0][1] as int, author: m[0][2].trim()]
    }
    return out
}


/* ===========================================================================
 *  PART 2 — trunk-per-commit  (worker; unchanged from the per-commit design)
 * ===========================================================================
 */

pipeline {
    agent { label 'windows' }

    parameters {
        string(name: 'REV',    defaultValue: '', description: 'SVN revision to build')
        string(name: 'AUTHOR', defaultValue: '', description: 'Commit author (svn username)')
    }

    stages {
        stage('Checkout @rev') {
            steps { bat "svn checkout -r ${params.REV} ^/trunk wc" }
        }
        stage('Build') {
            steps { dir('wc') { bat 'build ...' } }
        }
    }

    post {
        // Notify the single author of THEIR commit's result. AUTHOR comes from the
        // dispatcher's `svn log` — NOT from changeSets: a fresh -r REV checkout does
        // not yield a clean one-commit changeset (Jenkins diffs against the previous
        // build). Passing the author through is deterministic.
        unsuccessful { notifyAuthor() }           // failure / unstable / aborted
        // success   { notifyAuthor() }           // uncomment for green confirmations too
    }
}

// Domain hardcoded here because the worker builds the address explicitly (the
// "Default user e-mail suffix" trick only helps the provider-based batch version).
// If svn usernames don't map to mail local-parts, swap this for an author->email
// map lookup, and keep the domain in one place.
void notifyAuthor() {
    emailext(
        to:      "${params.AUTHOR}@your-domain.internal",
        subject: "trunk r${params.REV} — ${currentBuild.currentResult}",
        body:    "Revision ${params.REV} by ${params.AUTHOR}\n${env.BUILD_URL}"
    )
}


/* ===========================================================================
 *  Operational notes
 * ===========================================================================
 *
 *  RETENTION: "Discard old builds" can evict the build record holding the state.
 *  The description survives artifact-only discard (it's in the build record, not
 *  an artifact), so as long as retention keeps at least the most recent successful
 *  dispatcher build, readLastRev() finds the stamp. Under aggressive retention,
 *  add keepLog() on the dispatcher, or accept that a fully-rotated history
 *  re-bootstraps at HEAD (skipping the gap once).
 *
 *  REPLAY-OVER-SKIP: a failing worker still counts as handled (propagate:false =>
 *  dispatcher succeeds => HEAD advances past it), so you don't rebuild a broken rev
 *  in a loop. But if the dispatcher itself throws before stamping, the whole
 *  unhandled range is replayed next run — wasteful, safe. Make the worker tolerant
 *  of a re-trigger (a duplicate notify is the cost) if that matters.
 *
 *  BLAME: per-commit gives single-author attribution — r102 breaks trunk => only
 *  that author is pinged — which the batched build cannot do. Cost is the
 *  dispatcher/worker machinery and N builds per poll instead of one.
 * ===========================================================================
 */
