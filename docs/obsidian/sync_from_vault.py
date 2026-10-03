"""Export LARP study notes with portable Markdown links for GitHub and Obsidian."""
import argparse
import os
from pathlib import Path
import re
from urllib.parse import quote, unquote


def export(vault):
    destination = Path(__file__).resolve().parent
    project = destination.parent.parent
    notes = sorted(vault.glob('LARP - *.md'))
    if not notes:
        raise ValueError('the source directory contains no LARP study notes')
    names = {path.stem for path in notes}

    def source_link(match):
        source = Path(unquote(match.group(1)))
        if not source.is_relative_to(project) or not source.is_file():
            raise ValueError(f'source link is outside the checkout or missing: {source}')
        relative = os.path.relpath(source, destination)
        return '](' + quote(relative, safe='/') + ')'

    def note_link(match):
        target, separator, label = match.group(1).partition('|')
        name, anchor_separator, heading = target.partition('#')
        if name not in names:
            raise ValueError(f'unknown study note: {name}')
        link = quote(name + '.md', safe='')
        if anchor_separator:
            link += '#' + quote(heading.lower().replace(' ', '-'), safe='-')
        return '[' + (label if separator else target) + '](' + link + ')'

    for path in notes:
        text = path.read_text()
        text = re.sub(r'\]\(file://([^)]*)\)', source_link, text)
        text = re.sub(r'\[\[([^\]]+)\]\]', note_link, text)
        text = text.replace(str(project), '../..')
        (destination / path.name).write_text(text)
    print(f'Exported {len(notes)} LARP notes.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vault', type=Path, help='LARP note directory inside the Obsidian vault')
    export(parser.parse_args().vault)
