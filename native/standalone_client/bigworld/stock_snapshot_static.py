"""Owned, read-only JSON static inputs for the pinned snapshot decoder.

Only declared static actor fields cross this boundary. Dynamic dictionaries and
lists are still detached for every delivery. A value version owns its canonical
memo, so skipping a queued snapshot or rejecting its consumer does not lose a
static change. No transport sequence or accepted-client cache is used as a memo.
"""


STATIC_FIELDS = ('outfits', 'effective_params')

try:
    _row_items = dict.iteritems
except AttributeError:
    _row_items = dict.items


def _readonly(*unused_args, **unused_kwargs):
    raise TypeError('snapshot static values are read-only')


class _StaticDict(dict):
    __slots__ = ('__canonical', '__replay')
    __setitem__ = __delitem__ = clear = pop = popitem = setdefault = update = _readonly
    __ior__ = _readonly
    __setattr__ = __delattr__ = _readonly

    def __init__(self, value):
        if hasattr(self, '_StaticDict__canonical'):
            _readonly()
        dict.__init__(self, ((key, freeze(item)) for key, item in value.items()))
        object.__setattr__(self, '_StaticDict__canonical', {})
        object.__setattr__(self, '_StaticDict__replay', None)

    def normalized(self, normalizer):
        memo = self.__canonical
        if normalizer not in memo:
            value = normalizer(self)
            memo[normalizer] = None if value is None else freeze(value)
        return memo[normalizer]

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        # Explicit copies, notably replay recording, remain writable JSON.
        return detach(self)

    def _replay_plain(self):
        # Only the immediate replay pickler consumes this private projection.
        # Its bytes, never this mutable memo, cross the background queue.
        if self.__replay is None:
            object.__setattr__(self, '_StaticDict__replay', detach(self))
        return self.__replay


class _StaticList(list):
    __slots__ = ('__sealed', '__replay')
    __setitem__ = __delitem__ = append = extend = insert = pop = remove = _readonly
    __setslice__ = __delslice__ = __iadd__ = __imul__ = reverse = sort = _readonly

    __setattr__ = __delattr__ = _readonly

    def __init__(self, value):
        if hasattr(self, '_StaticList__sealed'):
            _readonly()
        list.__init__(self, (freeze(item) for item in value))
        object.__setattr__(self, '_StaticList__sealed', True)
        object.__setattr__(self, '_StaticList__replay', None)

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        return detach(self)

    def _replay_plain(self):
        if self.__replay is None:
            object.__setattr__(self, '_StaticList__replay', detach(self))
        return self.__replay


def freeze(value):
    """Take immutable ownership without retaining a mutable caller alias."""
    if isinstance(value, (_StaticDict, _StaticList)):
        return value
    if isinstance(value, dict):
        return _StaticDict(value)
    if isinstance(value, list):
        return _StaticList(value)
    if isinstance(value, tuple):
        return tuple(freeze(item) for item in value)
    return value


def detach(value):
    if isinstance(value, dict):
        return dict((key, detach(item)) for key, item in value.items())
    if isinstance(value, (list, tuple)):
        return [detach(item) for item in value]
    return value


def own_rows(rows):
    """Replace declared static values only; reused delta versions stay owned."""
    for row in rows:
        for name in STATIC_FIELDS:
            if name in row:
                row[name] = freeze(row[name])
    return rows


def detach_rows(rows, clone):
    # Actor rows are dominated by immutable scalars. Copy those in C, then
    # detach only plain mutable containers. Owned static subclasses deliberately
    # do not enter this branch. The decoder already validated every value.
    result = []
    for row in rows:
        copied = dict(row)
        for name, value in _row_items(row):
            kind = type(value)
            if kind is dict or kind is list or kind is tuple:
                copied[name] = clone(value)
        result.append(copied)
    return result


def canonical(source, cached_source, cached_value, normalizer, same):
    if isinstance(source, _StaticDict):
        # Worker projection feeds the prepared canonical value into the base
        # client again. Only owned, immutable values permit this identity proof.
        if source is cached_source and source is cached_value:
            return source
        return source.normalized(normalizer)
    if cached_value is not None and same(cached_source, source):
        return cached_value
    return normalizer(source)
