#!/bin/sh

# Ensure the script exits on the first error encountered
set -e

# Checkout to the specific commit hash
echo "Checking out to commit db858a3f..."
git checkout db858a3f

# Create and switch to a new branch for version 30.8.1
echo "Creating and switching to branch version-30.8.1..."
git checkout -b version-30.8.1

echo "Setup complete. You are now on branch version-30.8.1."
