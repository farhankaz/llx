#!/bin/bash
set -e

# Check if version is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.0.1"
    exit 1
fi

VERSION=$1
TAG="v${VERSION}"
REPO="farhankaz/llx"
TAP_REPO="homebrew-tap"
FORMULA_PATH="${TAP_REPO}/Formula/llx.rb"
ARCHIVE_URL="https://github.com/${REPO}/archive/refs/tags/${TAG}.tar.gz"

# Ensure we're on main branch
git checkout main

# Ensure working directory is clean
if [ -n "$(git status --porcelain)" ]; then
    echo "Error: Working directory is not clean. Please commit or stash changes."
    exit 1
fi

# Ensure tap repo exists as submodule
if [ ! -d "${TAP_REPO}" ]; then
    echo "Error: ${TAP_REPO} directory not found. Please set up the tap repository as a submodule first."
    exit 1
fi

# Update version in CMakeLists.txt
sed -i '' "s/set(LLX_VERSION \".*\")/set(LLX_VERSION \"${VERSION}\")/" CMakeLists.txt

# Commit version change
git add CMakeLists.txt
git commit -m "Bump version to ${VERSION}"

# Create and push tag
git tag -a "${TAG}" -m "Release ${TAG}"
git push origin main "${TAG}"

# Wait for GitHub to process the tag
echo "Waiting for GitHub to process the tag..."
sleep 10

# Calculate SHA256 hash
echo "Calculating SHA256 hash..."
HASH=$(curl -L "${ARCHIVE_URL}" | shasum -a 256 | cut -d ' ' -f 1)

if [ -z "${HASH}" ]; then
    echo "Error: Failed to calculate SHA256 hash"
    exit 1
fi

echo "SHA256 hash: ${HASH}"

# Update Homebrew formula
sed -i '' "s/sha256 \".*\"/sha256 \"${HASH}\"/" "${FORMULA_PATH}"

# Commit and push changes to tap repository
(cd "${TAP_REPO}" && \
    git add Formula/llx.rb && \
    git commit -m "Update llx to ${TAG}" && \
    git push)

# Update the submodule reference in the main repository
git add "${TAP_REPO}"
git commit -m "Update tap submodule to ${TAG}"
git push

echo "Release ${TAG} created successfully!"
echo "Users can install using:"
echo "brew tap ${REPO%/*}/tap"
echo "brew install llx" 