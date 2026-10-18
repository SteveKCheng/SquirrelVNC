# Instructions for AI agents working on SquirrelVNC

## Security and portability issues

The codebase SquirrelVNC is derived from, TightVNC, is old, and seems to be written in an era where
security, portability, and adherence to the best C++ coding practices, were not serious concerns.

It will take a while to address all the problems, so:

  1. Please be on a look-out for security issues.
  2. Report a summary of them whenever you encounter them.
  3. Whenever asked to fix security issues, if you find further cascading ones, you may continue fixing those.
  4. If in doubt about how to fix an issue or there are multiple approaches possible, please ask.
  4. Put security issues in separate commits, with a `[SECURITY]` prefix on the first line of the commit comment.
     - No need to go to a new branch — which might interrupt your other work.
     - This labelling is to leave an audit trail, makes the changes easier to review, especially if we need to 
       cross-reference against later versions of the source code that TightVNC might release.
     - If it's a portability fix: tag with a `[PORTABILITY]` prefix.
     - If an issue is arguable as to whether it's a security fix (e.g. because it has impacts 
       theoretically but happens to cause no issues on MSVC), just use reasonable judgement on the labelling.
  5. Write in any pre-conditions or post-conditions, as internal API docs, on any functions/classes you modify.
  6. If asked to write new code, and you're unsure of pre-conditions or post-conditions for some internal
     functionality, check more thoroughly and do not just blindly copy existing code patterns.

