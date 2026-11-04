"""Reliable native worker operations wait locally for their causal prerequisites."""
import copy


class Operations:
    def __init__(self, state, api):
        self.state = state
        self.api = api
        self.launches = {}
        self.contacts = {}
        self.finished = {}
        self.finished_later = {}

    def _done(self, key):
        return key[1] <= self.finished.get(key[0], 0) or key[1] in self.finished_later.get(key[0], ())

    def _complete(self, key):
        bot, sequence = key
        later = self.finished_later.setdefault(bot, set())
        later.add(sequence)
        last = self.finished.get(bot, 0)
        while last + 1 in later:
            last += 1
            later.remove(last)
        self.finished[bot] = last

    def launch(self, message):
        state = self.state
        with state.lock:
            bot = self.api['_exact_int'](message.get('shooter_id'), 1, self.api['PROJECTILE_MAX_ID'])
            sequence = self.api['_exact_int'](message.get('shot_seq'), 1, self.api['PROJECTILE_MAX_ID'])
            key = (bot, sequence)
            if message.get('shooter_kind') != 'bot':
                return state.launch_projectile(self.api['SIMULATION_WORKER_AUTHORITY_ID'], message)
            if self._done(key):
                return state.launch_projectile(self.api['SIMULATION_WORKER_AUTHORITY_ID'], message)
            if state.battle_result is not None:
                self._fail(key, 'battle_ended')
                return True
            previous = self.launches.get(key)
            if previous is not None and previous != message:
                return False
            if previous is None:
                if len(self.launches) >= self.api['PROJECTILE_MAX_ACTIVE']:
                    self._fail(key, 'pending_launch_capacity')
                    return True
                self.launches[key] = copy.deepcopy(message)
            self.advance()
            return True

    def contact(self, message):
        state = self.state
        with state.lock:
            key = (message.get('player_id'), message.get('contact_seq'))
            previous = self.contacts.get(key)
            if previous is not None and previous != message:
                return False
            if state.report_player_destructible_contact_result(
                    self.api['SIMULATION_WORKER_AUTHORITY_ID'], message):
                self.contacts.pop(key, None)
                return True
            reason = (state.last_destructible_contact_reject or {}).get('code')
            if reason != 'missing_publication':
                return False
            # Only an existing admitted sweep with its exact token can reach
            # missing_publication. Its native verdict stays frozen, once, here.
            self.contacts[key] = copy.deepcopy(message)
            return True

    def _fail(self, key, reason):
        bot, sequence = key
        if self._done(key):
            return
        state = self.state
        state.bot_pending_projectile_launches.discard(key)
        state.bot_pending_projectile_metadata.pop(key, None)
        state.pending_events.append(dict(kind='projectile_launch_failed',
            shooter_id=bot, shot_seq=sequence, round_id=state.round_id,
            authority_epoch=state.authority_epoch, reason=reason))
        self.api['_server_log']('BOT LAUNCH failed bot=%s seq=%s reason=%s' %
                                (bot, sequence, reason))
        self._complete(key)

    def finish(self):
        for key in sorted(self.launches):
            self._fail(key, 'battle_ended')
        self.launches.clear()
        for message in list(self.contacts.values()):
            authority = self.api['SIMULATION_WORKER_AUTHORITY_ID']
            # A terminal projectile may just have committed this dependency
            # before it ended the round. Preserve that successful verdict.
            if not self.state.report_player_destructible_contact_result(authority, message):
                self.state.report_player_destructible_contact_result(
                    authority, dict(message, accepted=False))
        self.contacts.clear()

    def advance(self):
        state = self.state
        with state.lock:
            if not state._combat_accepting() or state.battle_result is not None:
                return
            blocked = set()
            for key, message in sorted(list(self.launches.items())):
                bot, sequence = key
                if bot in blocked:
                    continue
                current = state.bot_states.get(bot)
                if current is not None and not current.get('alive') and key not in state.bot_pending_projectile_metadata:
                    self._fail(key, 'shooter_died_before_admission')
                    self.launches.pop(key, None)
                    continue
                if current is not None and sequence > int(current.get('fire_seq', 0)):
                    if current.get('alive'):
                        blocked.add(bot)
                        continue
                    self._fail(key, 'shooter_died_before_admission')
                    self.launches.pop(key, None)
                    continue
                if current is not None and key not in state.bot_pending_projectile_metadata:
                    # A clock/inventory rebase may skip inferred fire edges.
                    # The actual frozen native event, plus the admitted fired
                    # sequence, is the evidence; never invent a trajectory.
                    try:
                        edge = state._projectile_burst(message, sequence)
                        edge['shell_index'] = message.get('shell_index')
                        edge['launch_clock_offset_us'] = state.bot_launch_clock_offset_us
                        source = state.bot_source_time_us
                        if source is not None:
                            edge['presentation_clock_offset_us'] = state.bot_state_time_us - source
                        state.bot_pending_projectile_launches.add(key)
                        state.bot_pending_projectile_metadata[key] = edge
                    except (TypeError, ValueError, OverflowError):
                        self._fail(key, 'invalid_frozen_launch')
                        self.launches.pop(key, None)
                        continue
                accepted = state.launch_projectile(
                    self.api['SIMULATION_WORKER_AUTHORITY_ID'], message)
                if accepted:
                    projectile = state._projectile_id(state.round_id, 'bot', bot, sequence)
                    if projectile in state.projectiles or projectile in state.projectile_tombstones:
                        self._complete(key)
                    else:
                        self._fail(key, 'launch_not_admitted')
                    self.launches.pop(key, None)
                elif state.last_projectile_launch_reject_code in ('launch_edge_pending', 'order'):
                    blocked.add(bot)
                else:
                    self._fail(key, state.last_projectile_launch_reject_code or 'launch_rejected')
                    self.launches.pop(key, None)
            for key, message in list(self.contacts.items()):
                if state.report_player_destructible_contact_result(
                        self.api['SIMULATION_WORKER_AUTHORITY_ID'], message):
                    self.contacts.pop(key, None)
                elif (state.last_destructible_contact_reject or {}).get('code') != 'missing_publication':
                    self.contacts.pop(key, None)


def owner(state, api):
    key = (state.round_id, state.authority_epoch)
    cached = getattr(state, '_native_worker_operations', None)
    if cached is None or cached[0] != key:
        cached = (key, Operations(state, api))
        state._native_worker_operations = cached
    return cached[1]
