#!/bin/bash
revnumber=$(svnversion ${PWD})
echo \"$revnumber\" > svn-rev-number.txt

