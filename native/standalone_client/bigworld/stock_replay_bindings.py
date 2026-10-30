"""Adapt only the pinned recorder's immediate binary freeze boundary."""


def process(source):
    edits = (
        ('import uuid\n', 'import uuid\n\n'
         'from gui.mods.offline_lan_0922 import stock_replay_codec as _replay_codec\n'),
        ('        data = _pickle.dumps(value, 2)\n',
         '        data = _replay_codec.dumps(value, 2)\n'),
    )
    for before, after in edits:
        if source.count(before) != 1:
            raise ValueError('Pinned replay serialization boundary changed: ' + before)
        source = source.replace(before, after, 1)
    return source
