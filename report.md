# PES-VCS Lab Report

## Screenshots

(Add your screenshots here)

## Phase 5: Branching and Checkout

### Q5.1
To implement `pes checkout <branch>`, two things must change in `.pes/`:
1. Update `HEAD` to point to the new branch: `ref: refs/heads/<branch>`
2. Update the working directory to match the target branch's tree by reading the commit's tree object and restoring all files.

The complexity comes from handling modified files — if a file is modified in the working directory and differs between branches, checkout must refuse to avoid losing changes. Additionally, new files in the target branch must be created, and files that don't exist in the target branch must be deleted.

### Q5.2
To detect a dirty working directory conflict:
1. Read the current index (staged state)
2. Read the target branch's commit tree
3. For each file that differs between the two trees, check if the working directory version matches the index entry (using mtime and size)
4. If the working directory file differs from the index, the file is dirty and checkout must refuse

This works entirely using the index and object store — no re-hashing needed for the fast path.

### Q5.3
In detached HEAD state, HEAD contains a commit hash directly instead of a branch reference. New commits are made but no branch pointer is updated, so they become unreachable once you switch away. To recover, the user can create a new branch pointing to those commits using the commit hash shown during the session: `pes branch <new-branch> <hash>`.

## Phase 6: Garbage Collection

### Q6.1
Algorithm to find unreachable objects:
1. Start with all branch refs and HEAD as roots
2. Do a graph traversal: for each commit, mark it reachable, then mark its tree reachable, then recursively mark all blobs and subtrees reachable
3. Walk all objects in `.pes/objects/` and delete any not in the reachable set

A hash set (like a C hash table or in Python, a `set`) is ideal for tracking reachable hashes efficiently. For 100,000 commits with 50 branches, you'd need to visit roughly all 100,000 commits plus their trees and blobs — potentially millions of objects depending on repo size.

### Q6.2
Race condition: 
1. GC scans all objects and builds reachable set — snapshot taken at time T
2. A concurrent commit writes a new blob object at time T+1 (not yet referenced by any commit)
3. GC deletes the blob because it wasn't reachable at time T
4. The commit tries to reference the blob — it's gone, corruption occurs

Git avoids this by using a grace period — objects newer than 2 weeks are never deleted by GC, giving in-progress operations time to complete before objects become eligible for deletion.
