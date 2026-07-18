# Git workflow for this repo

The user does not want to deal with git concepts (branches, commits, pull
requests, review flow). Work directly on `main`:

- Never create a feature branch or a pull request.
- Never wait for review or ask for merge approval.
- After making changes, commit and push straight to `main` yourself, without
  asking permission each time.
- Keep history simple — no rebasing, no squash workflows, no force-push.

The working copy on disk is the source of truth: whatever the files say after
an edit is final, and GitHub should always end up matching it.
