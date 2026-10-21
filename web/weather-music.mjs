// Client-only weather soundtrack. Owns at most two streaming media elements and
// their gain nodes; all transitions run on the browser event loop. No game API,
// simulation clock or RNG is read or changed. dispose() ends this ownership.
export const WeatherMusicTracks = Object.freeze([
  { id: 'sunny', title: '晴山·松间初照', src: '/assets/music/sunny.mp3' },
  { id: 'rain', title: '听雨·檐下观心（柔雨版）', src: '/assets/music/rain.mp3' },
  { id: 'wind', title: '长风·山中问道', src: '/assets/music/wind.mp3' },
  { id: 'mist', title: '江雾·隔岸闻笛', src: '/assets/music/mist.mp3' },
  { id: 'clouds', title: '云霞·天光入岫', src: '/assets/music/clouds.mp3' },
].map(Track => Object.freeze(Track)));

const TracksById = new Map(WeatherMusicTracks.map(Track => [Track.id, Track]));
const TracksByWeather = new Map([
  ['春和景明', 'sunny'], ['秋高气爽', 'sunny'],
  ['烟雨绵延', 'rain'], ['灵雨润山', 'rain'],
  ['长风过境', 'wind'], ['朔风凛冽', 'wind'],
  ['薄雾锁江', 'mist'], ['云蒸霞蔚', 'clouds'], ['丹霞映天', 'clouds'],
]);
const StorageKey = 'nz-weather-music-v1';
const DefaultVolume = 0.25;
const LoadErrorText = '音乐暂时无法播放，请点击重试。';
const GestureErrorText = '请点击播放以允许背景音乐。';

export function mfTrackForWeather(Weather) {
  return TracksByWeather.get(Weather) ?? 'sunny';
}

function mfDefaultStorage() {
  try { return globalThis.localStorage; }
  catch { return null; } // Privacy mode can reject even reading the property.
}

function mfReadPreference(Storage) {
  const Preference = { enabled: false, volume: DefaultVolume, mode: 'auto' };
  try {
    const Saved = JSON.parse(Storage?.getItem(StorageKey) ?? 'null');
    if (!Saved || Saved.version !== 1) return Preference;
    if (typeof Saved.enabled === 'boolean') Preference.enabled = Saved.enabled;
    if (typeof Saved.volume === 'number' && Number.isFinite(Saved.volume) && Saved.volume >= 0 && Saved.volume <= 1) Preference.volume = Saved.volume;
    if (Saved.mode === 'auto' || TracksById.has(Saved.mode)) Preference.mode = Saved.mode;
  } catch { /* Optional browser preference; unavailable storage never blocks play. */ }
  return Preference;
}

// Injectable media, clock and document boundaries permit deterministic tests
// without creating an Audio/AudioContext at import time or while switched off.
export function mfCreateWeatherMusic(Options = {}) {
  const AudioFactory = Options.AudioFactory ?? (() => new globalThis.Audio());
  const ContextFactory = Object.hasOwn(Options, 'ContextFactory') ? Options.ContextFactory : () => {
    const Constructor = globalThis.AudioContext ?? globalThis.webkitAudioContext;
    return Constructor ? new Constructor() : null;
  };
  const Storage = Object.hasOwn(Options, 'Storage') ? Options.Storage : mfDefaultStorage();
  const Document = Options.Document ?? globalThis.document;
  const Now = Options.Now ?? (() => globalThis.performance?.now() ?? Date.now());
  const Raf = Options.Raf ?? (Callback => globalThis.requestAnimationFrame(Callback));
  const Caf = Options.Caf ?? (Frame => globalThis.cancelAnimationFrame(Frame));
  const FadeMilliseconds = Number.isFinite(Options.FadeMilliseconds) ? Math.max(0, Options.FadeMilliseconds) : 4000;
  const Preference = mfReadPreference(Storage);
  const Subscribers = new Set();
  let Enabled = Preference.enabled;
  let Volume = Preference.volume;
  let Mode = Preference.mode;
  let Weather = '';
  let Status = Enabled ? 'waiting' : 'off';
  let ErrorText = '';
  let Unlocked = false;
  let Disposed = false;
  let Context = null;
  let ContextAttempted = false;
  let Active = null;
  let Incoming = null;
  let Transition = 0;
  let FadeFrame = null;
  let PendingPromise = Promise.resolve();

  function mfSelectedTrack() { return TracksById.get(Mode === 'auto' ? mfTrackForWeather(Weather) : Mode); }
  function mfSnapshot() {
    const Track = mfSelectedTrack();
    return Object.freeze({ enabled: Enabled, volume: Volume, mode: Mode, weather: Weather,
      trackId: Track.id, title: Track.title, status: Status, error: ErrorText });
  }
  function mfPublish() {
    if (Disposed) return;
    const Snapshot = mfSnapshot();
    for (const Subscriber of [...Subscribers]) {
      try { Subscriber(Snapshot); }
      catch { /* A view subscriber cannot interrupt resource cleanup or gameplay. */ }
    }
  }
  function mfPersist() {
    try { Storage?.setItem(StorageKey, JSON.stringify({ version: 1, enabled: Enabled, volume: Volume, mode: Mode })); }
    catch { /* Preference writes are optional in private/quota-limited storage. */ }
  }
  function mfSetWeight(Slot, Weight) {
    if (!Slot || Slot.Released) return;
    Slot.Weight = Weight;
    if (Slot.Gain) Slot.Gain.gain.value = Volume * Weight;
    else Slot.Audio.volume = Volume * Weight;
  }
  function mfRelease(Slot) {
    if (!Slot) return;
    // Repeat pause after a stale play promise resolves: the browser may have
    // restarted an element even though its previous source was already cleared.
    try { Slot.Audio.pause(); } catch { /* Detached media implementation. */ }
    if (Slot.Released) return;
    Slot.Released = true;
    Slot.Audio.removeEventListener?.('error', Slot.OnError);
    try { Slot.Audio.removeAttribute('src'); Slot.Audio.load(); } catch { /* Release still disconnects the graph below. */ }
    try { Slot.Source?.disconnect(); } catch { /* Already disconnected. */ }
    try { Slot.Gain?.disconnect(); } catch { /* Already disconnected. */ }
  }
  function mfCancelFrame() {
    if (FadeFrame !== null) { Caf(FadeFrame); FadeFrame = null; }
  }
  function mfKeepOne() {
    mfCancelFrame();
    if (Incoming) {
      if (Incoming.Playing && (!Active || Incoming.Weight > Active.Weight)) {
        mfRelease(Active);
        Active = Incoming;
      } else mfRelease(Incoming);
      Incoming = null;
    }
    mfSetWeight(Active, 1);
  }
  function mfEnsureContext() {
    if (!ContextAttempted) {
      ContextAttempted = true;
      try { Context = ContextFactory?.() ?? null; }
      catch { Context = null; } // Older browsers can fall back to element volume.
    }
    return Context;
  }
  function mfResumeContext() {
    const AudioContext = mfEnsureContext();
    try {
      return Promise.resolve(AudioContext && AudioContext.state !== 'running' ? AudioContext.resume() : undefined);
    } catch (Error) { return Promise.reject(Error); }
  }
  function mfFailure(Slot, Error) {
    if (!Slot || Disposed || Slot.Released || (Slot !== Active && Slot !== Incoming)) return;
    Transition += 1;
    mfCancelFrame();
    if (Slot === Incoming) {
      mfRelease(Incoming);
      Incoming = null;
      mfSetWeight(Active, 1);
    } else {
      mfRelease(Active);
      Active = Incoming?.Playing ? Incoming : null;
      if (!Active) mfRelease(Incoming);
      Incoming = null;
      mfSetWeight(Active, 1);
    }
    const Blocked = Error?.name === 'NotAllowedError';
    if (Blocked) Unlocked = false;
    ErrorText = Blocked ? GestureErrorText : LoadErrorText;
    Status = Active?.Playing ? 'playing' : Blocked ? 'waiting' : 'error';
    mfPublish();
  }
  function mfCreateSlot(Track) {
    const AudioValue = AudioFactory();
    const Slot = { Audio: AudioValue, TrackId: Track.id, Weight: 0, Gain: null, Source: null,
      Playing: false, Released: false, PendingToken: null, OnError: null };
    try {
      AudioValue.preload = 'none';
      AudioValue.loop = true;
      AudioValue.src = Track.src;
      if (Context) {
        Slot.Gain = Context.createGain();
        Slot.Gain.gain.value = 0;
        Slot.Source = Context.createMediaElementSource(AudioValue);
        Slot.Source.connect(Slot.Gain);
        Slot.Gain.connect(Context.destination);
        AudioValue.volume = 1;
      } else AudioValue.volume = 0;
      Slot.OnError = () => mfFailure(Slot, AudioValue.error);
      AudioValue.addEventListener?.('error', Slot.OnError);
      return Slot;
    } catch (Error) {
      mfRelease(Slot);
      throw Error;
    }
  }
  function mfFade(Token) {
    const StartedAt = Now();
    function mfStep() {
      FadeFrame = null;
      if (Disposed || Token !== Transition || !Incoming || !Active) return;
      const Progress = FadeMilliseconds === 0 ? 1 : Math.min(1, Math.max(0, (Now() - StartedAt) / FadeMilliseconds));
      mfSetWeight(Active, 1 - Progress);
      mfSetWeight(Incoming, Progress);
      if (Progress === 1) {
        mfRelease(Active);
        Active = Incoming;
        Incoming = null;
      } else FadeFrame = Raf(mfStep);
    }
    mfStep();
  }
  function mfStart(ResumePromise = Promise.resolve()) {
    if (Disposed || !Enabled || !Unlocked || Document?.hidden) {
      // Attach a handler even when a visibility change prevents playback.
      ResumePromise.catch(() => {});
      if (!Disposed && Enabled && Document?.hidden) { Status = 'paused'; mfPublish(); }
      return Promise.resolve();
    }
    const Track = mfSelectedTrack();
    if (Incoming?.TrackId === Track.id) {
      ResumePromise.catch(() => {});
      return PendingPromise;
    }
    if (Active?.TrackId === Track.id && Active.PendingToken === Transition) {
      ResumePromise.catch(() => {});
      return PendingPromise;
    }
    if (Active?.TrackId === Track.id && Active.Playing && !Active.Audio.paused && !Incoming) {
      const PlayingSlot = Active;
      if (ErrorText || Status !== 'playing') {
        ErrorText = '';
        Status = 'playing';
        mfPublish();
      }
      return ResumePromise.catch(Error => mfFailure(PlayingSlot, Error));
    }
    Transition += 1;
    const Token = Transition;
    mfKeepOne();
    ErrorText = '';
    let Slot;
    let Resuming = false;
    try {
      if (Active?.TrackId === Track.id) { Slot = Active; Resuming = true; }
      else { Slot = mfCreateSlot(Track); Incoming = Slot; }
      Status = 'loading';
      mfPublish();
      Slot.PendingToken = Token;
      // Both are invoked in this stack, before awaiting, to preserve the gesture.
      const Played = Slot.Audio.play();
      PendingPromise = Promise.all([ResumePromise, Promise.resolve(Played)]).then(() => {
        if (Slot.PendingToken === Token) Slot.PendingToken = null;
        if (Disposed || Token !== Transition || !Enabled) {
          if (Slot !== Active && Slot !== Incoming) mfRelease(Slot);
          else if (Document?.hidden || !Enabled) Slot.Audio.pause();
          return;
        }
        Slot.Playing = true;
        if (Document?.hidden) { Slot.Audio.pause(); Status = 'paused'; mfPublish(); return; }
        if (Resuming || !Active) {
          Active = Slot;
          if (Incoming === Slot) Incoming = null;
          mfSetWeight(Active, 1);
        } else mfFade(Token);
        Status = 'playing';
        ErrorText = '';
        mfPublish();
      }).catch(Error => {
        if (Slot.PendingToken === Token) Slot.PendingToken = null;
        if (Disposed || Token !== Transition) {
          if (Slot !== Active && Slot !== Incoming) mfRelease(Slot);
          return;
        }
        mfFailure(Slot, Error);
      });
    } catch (Error) {
      ResumePromise.catch(() => {});
      if (Slot) mfFailure(Slot, Error);
      else { ErrorText = LoadErrorText; Status = Active?.Playing ? 'playing' : 'error'; mfPublish(); }
      PendingPromise = Promise.resolve();
    }
    return PendingPromise;
  }
  function mfUnlock() {
    if (Disposed || !Enabled) return Promise.resolve();
    Unlocked = true;
    return mfStart(mfResumeContext());
  }
  function mfSelectionChanged() {
    mfPersist();
    mfPublish();
    if (Enabled && Unlocked) mfStart();
  }
  function mfGesture() {
    if (Enabled && !Unlocked) mfUnlock();
  }
  function mfVisibilityChanged() {
    if (Disposed || !Enabled) return;
    if (Document.hidden) {
      Transition += 1;
      mfKeepOne();
      Active?.Audio.pause();
      Status = 'paused';
      mfPublish();
    } else if (Unlocked) mfStart(mfResumeContext());
    else { Status = 'waiting'; mfPublish(); }
  }
  Document?.addEventListener('pointerdown', mfGesture, { capture: true, passive: true });
  Document?.addEventListener('keydown', mfGesture, { capture: true, passive: true });
  Document?.addEventListener('visibilitychange', mfVisibilityChanged);

  return Object.freeze({
    setWeather(NextWeather) {
      if (Disposed) return;
      Weather = typeof NextWeather === 'string' ? NextWeather : '';
      mfSelectionChanged();
    },
    setEnabled(NextEnabled) {
      if (Disposed || typeof NextEnabled !== 'boolean') return Promise.resolve();
      Enabled = NextEnabled;
      mfPersist();
      if (Enabled) return mfUnlock();
      Transition += 1;
      Unlocked = false;
      mfCancelFrame();
      mfRelease(Active);
      mfRelease(Incoming);
      Active = null;
      Incoming = null;
      Status = 'off';
      ErrorText = '';
      mfPublish();
      return Promise.resolve();
    },
    setVolume(NextVolume) {
      if (Disposed || typeof NextVolume !== 'number' || !Number.isFinite(NextVolume)) return;
      Volume = Math.min(1, Math.max(0, NextVolume));
      mfSetWeight(Active, Active?.Weight ?? 0);
      mfSetWeight(Incoming, Incoming?.Weight ?? 0);
      mfPersist();
      mfPublish();
    },
    setMode(NextMode) {
      if (Disposed || (NextMode !== 'auto' && !TracksById.has(NextMode))) return;
      Mode = NextMode;
      mfSelectionChanged();
    },
    unlock: mfUnlock,
    getSnapshot: mfSnapshot,
    subscribe(Subscriber) {
      if (Disposed || typeof Subscriber !== 'function') return () => {};
      Subscribers.add(Subscriber);
      try { Subscriber(mfSnapshot()); } catch { /* Same isolation as update notifications. */ }
      return () => Subscribers.delete(Subscriber);
    },
    dispose() {
      if (Disposed) return;
      Disposed = true;
      Enabled = false;
      Status = 'off';
      Transition += 1;
      mfCancelFrame();
      mfRelease(Active);
      mfRelease(Incoming);
      Active = null;
      Incoming = null;
      Subscribers.clear();
      Document?.removeEventListener('pointerdown', mfGesture, true);
      Document?.removeEventListener('keydown', mfGesture, true);
      Document?.removeEventListener('visibilitychange', mfVisibilityChanged);
      try { Promise.resolve(Context?.close()).catch(() => {}); } catch { /* Closing an already closed context is harmless. */ }
      Context = null;
    },
  });
}
