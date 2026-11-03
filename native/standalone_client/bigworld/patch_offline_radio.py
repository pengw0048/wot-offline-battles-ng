"""Adapt the pinned control core to the reviewed radio/spotting contract."""


def _replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('Unexpected offline simulation control source: ' + old[:80])
    return source.replace(old, new, 1)


def patch_header(source):
    return _replace_once(source, '  double designated_duration = 12.;\n',
        '  double designated_duration = 12.;\n  double designated_sector = 0.08726646259971647;\n')


def patch(source):
    source = _replace_once(source,
        '} else if (!a.radio_retired || now_ < a.vengeance_until) {',
        '} else if (!a.radio_retired) {')
    source = _replace_once(source,
        '''  if (!owner->second.sample.alive &&
      owner->second.config.team == actors_.at(recipient).config.team &&
      owner->second.retired_receivers.count(recipient))
    return true;
  return connected(recipient, source);''',
        '''  if (!owner->second.sample.alive)
    return owner->second.config.team == actors_.at(recipient).config.team &&
           owner->second.retired_receivers.count(recipient);
  return connected(recipient, source);''')
    return _replace_once(source,
        'std::abs(angle(std::atan2(dx, dz), gun)) <= pi / 36. + 1e-9)',
        'std::abs(angle(std::atan2(dx, dz), gun)) <= s.config.designated_sector + 1e-9)')
