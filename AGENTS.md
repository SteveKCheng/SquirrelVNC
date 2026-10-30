# Instructions for AI agents working on SquirrelVNC

## Security and portability issues

The codebase SquirrelVNC is derived from, TightVNC, is old, and seems not to have much prioritized security, portability, robustness, and adherence to standards.

It will take a while to address all the problems, so:

  1. Please be on a look-out for buggy code, especially those with security implications.
  2. Summarize issues whenever you encounter them.
  3. Whenever asked to fix the issues, if you find further cascading ones, you may continue fixing those.
  4. If in doubt about how to fix an issue, or when there are multiple approaches possible, please ask.
  4. Put fixes in separate commits, with a prefix (described below) on the first line of the commit comment.
     - No need to go to a new branch — which might interrupt your other work.
     - This labelling is to leave an audit trail, makes the changes easier to review, especially if we need to cross-reference against later versions of the source code that TightVNC might release.
  5. Write in any pre-conditions or post-conditions, on any functions/classes you modify
  6. If asked to write new code, and you're unsure of pre-conditions or post-conditions for some internal functionality, check more thoroughly, and do not just blindly copy existing code patterns.

### Prefix labels on revision comments

  - `[SECURITY]`: bad or undefined behavior, that should not happen if the code correctly defended against the outside threat
    - Threat model is detailed in `notes/security.md`
    - This category subsumes memory corruption issues, even if they may be hard to exploit. (It's not worth our effort to distinguish severities more finely.)
  - `[PORTABILITY]`: use of platform-specific behavior, or accidental but deterministic behavior of a certain compiler or run-time
  - `[LEAK]`: resource or memory leaks

If the category of an issue is arguable, use reasonable judgement on the labelling.

## Building

  - To save time, unless specifically required otherwise, you may build these project configurations only:
    - `Debug` `Win32`. 32-bit x86 is not a first-tier platform for SquirrelVNC; this is only to catch errors on C/C++ types that switch between 32-bit and 64-bit.
    - `Debug` `x64`. Makes sure any conditionally-compiled code for Debug builds do work; also not something we need to run normally.
    - `ReleaseAsan` `x64`.  Main configuration for iterative development and running console-based test programs.
  - To run the fuzzers, you need to build in `ReleaseFuzz` (working only under `x64` is fine).`
  - You will be developing in a sandboxed environment
    - If you need anything not available in the sandbox, ask first before trying to work around the problem

## Coding style and standards

  - The codebase has been switched to compile with C++20
    - In practice, we only use features supported by both MSVC and Clang (MSVC ABI)
  - We inherit TightVNC's conventions on naming identifiers
  - But strive to write new code to modern best C++ practices
    - A good reference: [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
  - We are slowly switching `core-lib` to use portable, standard C++
    - But don't change the style of the existing code haphazardly
    - Changes for security / portability / re-factoring need to be cleanly separated in revision history
  - Use the static type system to make wrong code appear obviously wrong, or ideally not even compile
  - Do not over-generalize frameworks
    - We aggressively delete dead code to save work in maintenance, and make the codebase easier to analyze

Finer points on style:

  - Write documention on classes and methods in Doxygen format
  - `ScopeExit` is a local scope guard for destroying resources
  - Prefer references (`&`) in method arguments, over raw pointers 
    - Implies that the target is non-null and is being borrowed; explicit documentation not needed
    - Exceptions and further details to this rule are described in the [note on pointer parameters](notes/pointer-parameters.md)
    - But you don't need to remember all of the note; much of the code already has been converted to the preferred style, so you can imitate what's there already
  - Use smart pointers where possible/practical to imply other ownership semantics
  - Classes that should not be inherited from are `final`
    - Exception: struct-like types (e.g. as inherited from C `struct`) do not need `final`
  - Implementations of virtual methods are `override`
  - Delete copy constructors/assignment on ‘non-value’ types; even move constructors/assignment if not needed
  - Base classes that are not usefully instantiated have pure `virtual` methods or protected constructors

## Design and planning

  - For major work, we write down plans and conclusions in [`notes/`](notes/README.md)
    - Update relevant notes as we change/fix code
    - Historical information is okay to keep; everyone can then understand how decisions were arrived at and why 
    - Notes provide context for later work to resume (in other sessions/agents)
  - Feel free to suggest re-factoring, fixes, etc. as we go along

