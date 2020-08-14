#!/bin/bash
revnumber=$(svnversion ${PWD})
echo \"$revnumber\" > GlobalRevNumber.txt

