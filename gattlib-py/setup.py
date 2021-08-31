#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2021, Olivier Martin <olivier@labapart.org>
#

import os
from setuptools import setup, find_packages
import subprocess

# Retrieve version from GIT
git_version_command = subprocess.Popen(['git', 'describe', '--abbrev=7', '--dirty', '--always', '--tags'],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
stdout, stderr = git_version_command.communicate()
git_version = stdout.decode('utf-8').strip()

# Value from travis-ci
if 'TRAVIS_TAG' in os.environ:
    git_version = os.environ['TRAVIS_TAG']
elif 'TRAVIS_BUILD_ID' in os.environ:
    git_version = os.environ['TRAVIS_BUILD_ID'] + '-' + git_version

with open("README.md", "r") as fh:
    long_description = fh.read()

setup(
    name='gattlib-py',
    version=git_version,
    author="Olivier Martin",
    author_email="olivier@labapart.com",
    description="Python wrapper for gattlib library",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/labapart/gattlib/gattlib-py",
    packages=find_packages(),
    install_requires=['setuptools'],
)
