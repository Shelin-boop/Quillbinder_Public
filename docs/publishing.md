# Publishing Quillbinder

How the public source and the public releases are made (since 0.60).

## One source, two repositories

Quillbinder is developed in a private repository. The public repository
(https://github.com/Shelin-boop/Quillbinder_Public) holds a copy of the committed files of that
repository, one commit per published version. The two repositories share no history: the public
repository's history is only the list of published versions.

Every change is made once, in the development repository. Publishing it is one command there:

    tools/publish-public.sh <clone of Quillbinder_Public> --commit

The script

1. exports the committed files of the development repository (never its history);
2. leaves out the paths listed in `tools/public-exclude.txt` (the detailed development log, the tool that
   stores files in the development repository) and the text between the lines `<!-- private:begin -->` and
   `<!-- private:end -->` of Markdown and text files (for example the parts of `CLAUDE.md` that describe
   the private working setup);
3. runs `tools/legal-check.py --public` on the export and stops if any check fails;
4. makes the files of the public clone equal to the export and commits them as "Quillbinder
   <version> <label>", naming the development commit it came from.

Nothing is pushed by the script. Pushing the public `main` branch (`git push origin main` in the public
clone) is a separate step. A new version on the public `main` starts `.github/workflows/release.yml`
there: it builds the AppImage, the program update and the add-ons in a clean Ubuntu 24.04, checks the
AppImage, collects the source code of every third-party library and publishes the release. The workflow
runs only in the public repository.

## What the checks look for

`tools/legal-check.py` guards against known kinds of mistakes before anything is published. It is not a
legal review.

- `LICENSE` is the Zero-Clause BSD license and names no person.
- The README, the AppStream description, Help > About and the release text say that Quillbinder is not
  made by or connected with Literature & Latte, the maker of Scrivener and Scapple.
- No wording suggests a copy of Scrivener, a replacement for it, or a link with its maker; Scrivener's own
  names for parts of its program are not used (the old settings keys of earlier Quillbinder versions are
  still read).
- Nothing of an earlier tutorial and no earlier pen name is left.
- There are no project, board or template files of Scrivener or Scapple, and no pictures named after them.
- Every picture, sound, video, font and document file has a note on where it comes from (a README,
  LICENSE or COPYING file in its folder or a folder above it); every font has its license beside it.
- FFmpeg is built without GPL or non-free parts, and Help > About and the media add-ons name it with its
  license (GNU LGPL 2.1 or later).
- `THIRD-PARTY.md` names Quillbinder's license, the main third-party parts and the release with their
  source code.
- In the public copy: no private files and no private blocks.

## Rules for the source

- Scrivener and Scapple are named only where their file formats or the import are meant.
- No text, picture, code, template, layout or list of fields is copied from Scrivener, Scapple, their
  manuals, templates or tutorials, also not from screenshots: functions are described and designed in
  words and choices of Quillbinder's own.
- Every file that is not source code says where it comes from and under which license.
- The source code of every third-party library is published in exactly the version used (release
  "Third-party source code").
