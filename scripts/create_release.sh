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
ARCHIVE_URL="https://github.com/${REPO}/archive/refs/tags/${TAG}.tar.gz"

# Ensure we're on main branch
git checkout main

# Ensure working directory is clean
if [ -n "$(git status --porcelain)" ]; then
    echo "Error: Working directory is not clean. Please commit or stash changes."
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
sed -i '' "s/sha256 \".*\"/sha256 \"${HASH}\"/" llx.rb

echo "Release ${TAG} created successfully!"
echo "Next steps:"
echo "1. Create a new repository named 'homebrew-tap' if you haven't already"
echo "2. Copy the updated llx.rb to your homebrew-tap repository"
echo "3. Push the changes to GitHub"
echo ""
echo "Users can then install using:"
echo "brew tap ${REPO%/*}/tap"
echo "brew install llx" 