#!/bin/sh
git rev-list --all --objects | git cat-file --batch-check='%(objectname) %(objecttype) %(objectsize) %(rest)' | grep blob | sort -k3nr | head -n 20
