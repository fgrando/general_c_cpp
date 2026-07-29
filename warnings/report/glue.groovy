bat 'python warntable.py build.log --waivers waivers.csv -o reports\\warnings.csv'
bat """python report.py --warnings reports\\warnings.csv ^
        --title "Warnings & Static Analysis" ^
        --job "%JOB_NAME%" --build %BUILD_NUMBER% ^
        --status ${currentBuild.currentResult} --rev "%SVN_REVISION%" ^
        --url "%BUILD_URL%" ^
        --analysis "cppcheck:reports\\cppcheck.csv" ^
        --analysis "clang-tidy:reports\\clang_tidy.csv" ^
        --outdir reports"""

publishHTML(target: [reportDir: 'reports', reportFiles: 'report.html',
                     reportName: 'Quality Report', keepAll: true,
                     alwaysLinkToLastBuild: true, allowMissing: false])