#!/bin/bash

git config --global user.email "olivier@labapart.com"
git config --global user.name "Olivier Martin"
git tag -f dev -a -m "Generated tag from TravisCI build $TRAVIS_BUILD_NUMBER"
git push --quiet https://${GITHUB_OAUTH_TOKEN}@github.com/labapart/gattlib --force dev > /dev/null 2>&1
