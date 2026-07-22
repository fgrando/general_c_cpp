// vars/mountFolder.groovy
//
// Jenkins shared-library step: mount a folder to the first available drive
// letter on a Windows agent using SUBST.
//
// Usage from a pipeline (Windows agent required):
//
//     def drive = mountFolder(
//         folder: 'C:\\Users\\fgrando\\projects',
//         drives: ['X', 'Y', 'Z']
//     )
//     echo "Project is on ${drive}"      // e.g. "X:"
//
// Parameters (Map):
//     folder         (String, required)  Path to mount. Must already exist.
//     drives         (List,   required)  Candidate drive letters, in order.
//                                         Accepts 'X' or 'X:' forms.
//     reuseExisting  (boolean, optional)  Default false. If the folder is
//                                         already substituted, false throws
//                                         (original "error 2" behaviour),
//                                         true returns the existing drive.
//
// Returns: the chosen drive as a String, e.g. "X:".
//
// Throws (all fail the build with a clear message):
//     - folder does not exist                 (was exit 1)
//     - folder already mounted & !reuseExisting(was exit 2)
//     - none of the informed drives available (was exit 3)
//
def call(Map config = [:]) {
    String folder = config.folder
    List drives   = (config.drives ?: []) as List
    boolean reuseExisting = config.get('reuseExisting', false)

    if (!folder) { error 'mountFolder: "folder" parameter is required.' }
    if (!drives) { error 'mountFolder: "drives" list is required.' }
    if (isUnix()) {
        error 'mountFolder: SUBST is Windows-only; run this step on a Windows agent.'
    }

    // --- (1) Folder must exist -------------------------------------------
    if (!fileExists(folder)) {
        error "mountFolder: folder does not exist: ${folder}"
    }

    // Canonicalize to a full path (no trailing slash) the same way %~f1 would.
    String full = bat(
        returnStdout: true,
        script: "@echo off\r\nfor %%I in (\"${folder}\") do @echo %%~fI"
    ).trim()

    // --- (2) Already mounted via SUBST? ----------------------------------
    // SUBST output line format:  Z:\: => C:\path\to\folder
    String substOut = bat(returnStdout: true, script: '@echo off\r\nsubst').trim()
    String mountedOn = null
    substOut.readLines().each { String line ->
        int idx = line.indexOf('=>')
        if (idx >= 0) {
            String tgt = line.substring(idx + 2).trim()          // "C:\path"
            if (tgt.equalsIgnoreCase(full)) {
                mountedOn = line.substring(0, idx).trim().take(2) // "Z:"
            }
        }
    }
    if (mountedOn) {
        if (reuseExisting) {
            echo "mountFolder: already mounted on ${mountedOn}, reusing."
            return mountedOn
        }
        error "mountFolder: folder already mounted on ${mountedOn}: ${full}"
    }

    // --- (3) First available drive from the list -------------------------
    String chosen = null
    for (d in drives) {
        String letter = (d ?: '').toString().trim().take(1).toUpperCase()
        if (!letter) { continue }
        String drv = "${letter}:"

        // Skip letters already in use (real, network or subst).
        int inUse = bat(
            returnStatus: true,
            script: "@if exist ${drv}\\ (exit /b 1) else (exit /b 0)"
        )
        if (inUse != 0) {
            echo "mountFolder: ${drv} is in use, skipping."
            continue
        }

        int rc = bat(returnStatus: true, script: "@subst ${drv} \"${full}\"")
        if (rc == 0) {
            chosen = drv
            break
        }
        echo "mountFolder: subst failed on ${drv} (rc=${rc}), trying next."
    }

    if (!chosen) {
        error "mountFolder: none of the informed drive letters is available: ${drives}"
    }

    echo "mountFolder: mounted ${full} on ${chosen}"
    return chosen
}
