# Command plugins

A command plugin adds a compile format by running a program that converts the
DOCX (or Markdown) file the program writes first. No programming is needed:
copy a `.json` file into the plugin folder (Help > Open Plugin Folder) and
restart. Formats whose program is not installed are listed in the Compile
window as unavailable.

| Key         | Meaning |
|-------------|---------|
| `id`        | Unique name, lowercase, no spaces. |
| `name`      | Shown in the "Compile for" list. |
| `extension` | File ending of the result, without dot. |
| `input`     | `docx` (default) or `md`: what the program receives. |
| `program` / `programs` | Program to run; with `programs`, the first one installed is used. |
| `arguments` | Arguments. `{input}` input file, `{inputbase}` its name without ending, `{outdir}` output folder, `{output}` expected output file, `{tmpdir}` a temporary folder removed afterwards, `{title}`, `{author}`. |
| `output`    | Where the program writes its result, if not `{output}` (for example `{outdir}/{inputbase}.pdf`). |
| `timeout`   | Seconds before giving up (default 180). |
