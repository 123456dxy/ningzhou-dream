// Browser media boundaries are injected; the controller and its state transitions are real.
import assert from 'node:assert/strict';

let Music;
try { Music = await import('../web/weather-music.mjs'); }
catch (Error) { assert.fail(`Weather music module is unavailable: ${Error.message}`); }
const { WeatherMusicTracks, mfTrackForWeather, mfCreateWeatherMusic } = Music;
let Checks = 0;
async function mfCheck(Name, Test) {
  await Test();
  Checks += 1;
  console.log(`PASS ${Name}`);
}
const mfFlush = async () => { for (let Step = 0; Step < 8; Step += 1) await Promise.resolve(); };
function mfDeferred() {
  let Resolve;
  let Reject;
  const PromiseValue = new Promise((ResolveValue, RejectValue) => { Resolve = ResolveValue; Reject = RejectValue; });
  return { PromiseValue, Resolve, Reject };
}

class TestAudio extends EventTarget {
  constructor(Environment) {
    super();
    this.Environment = Environment;
    this.src = '';
    this.volume = 1;
    this.currentTime = 0;
    this.paused = true;
    this.PlayCount = 0;
    this.PauseCount = 0;
    this.LoadCount = 0;
  }
  play() {
    this.PlayCount += 1;
    const Outcome = this.Environment.PlayOutcomes.shift();
    if (Outcome instanceof Error) return Promise.reject(Outcome);
    if (Outcome) return Outcome.then(() => { this.paused = false; });
    this.paused = false;
    return Promise.resolve();
  }
  pause() { this.paused = true; this.PauseCount += 1; }
  load() { this.LoadCount += 1; }
  removeAttribute(Name) { if (Name === 'src') this.src = ''; }
}

function mfEnvironment(Saved = null, Extra = {}) {
  const Environment = {
    Audios: [], PlayOutcomes: [], Frames: new Map(), Time: 0, NextFrame: 0,
    Saved, Writes: 0, Contexts: [],
  };
  const Document = new EventTarget();
  Document.hidden = false;
  const Options = {
    AudioFactory: () => { const AudioValue = new TestAudio(Environment); Environment.Audios.push(AudioValue); return AudioValue; },
    ContextFactory: null,
    Storage: { getItem: () => Environment.Saved, setItem: (Key, Value) => { assert.equal(Key, 'nz-weather-music-v1'); Environment.Saved = Value; Environment.Writes += 1; } },
    Document,
    Now: () => Environment.Time,
    Raf: Callback => { const Id = ++Environment.NextFrame; Environment.Frames.set(Id, Callback); return Id; },
    Caf: Id => Environment.Frames.delete(Id),
    FadeMilliseconds: 4000,
    ...Extra,
  };
  Environment.Controller = mfCreateWeatherMusic(Options);
  Environment.Document = Document;
  Environment.Advance = async Milliseconds => {
    Environment.Time += Milliseconds;
    const Frames = [...Environment.Frames.values()];
    Environment.Frames.clear();
    Frames.forEach(Callback => Callback(Environment.Time));
    await mfFlush();
  };
  return Environment;
}

await mfCheck('nine weather labels select the five approved tracks', () => {
  for (const [Weather, Track] of [
    ['春和景明', 'sunny'], ['秋高气爽', 'sunny'], ['烟雨绵延', 'rain'],
    ['灵雨润山', 'rain'], ['长风过境', 'wind'], ['朔风凛冽', 'wind'],
    ['薄雾锁江', 'mist'], ['云蒸霞蔚', 'clouds'], ['丹霞映天', 'clouds'],
    ['', 'sunny'], ['不认识的天气', 'sunny'], [null, 'sunny'],
  ]) assert.equal(mfTrackForWeather(Weather), Track);
  assert.equal(WeatherMusicTracks.length, 5);
  assert.ok(Object.isFrozen(WeatherMusicTracks));
  assert.ok(WeatherMusicTracks.every(Track => Object.isFrozen(Track) && /^\/assets\/music\/[a-z]+\.mp3$/.test(Track.src)));
});

await mfCheck('disabled state does not create, load or unlock media', async () => {
  const Environment = mfEnvironment();
  const Controller = Environment.Controller;
  Controller.setWeather('烟雨绵延');
  await Controller.unlock();
  Environment.Document.dispatchEvent(new Event('pointerdown'));
  assert.equal(Environment.Audios.length, 0);
  assert.deepEqual(Controller.getSnapshot(), { enabled: false, volume: 0.25, mode: 'auto', weather: '烟雨绵延', trackId: 'rain', title: '听雨·檐下观心（柔雨版）', status: 'off', error: '' });
  Controller.dispose();
});

await mfCheck('enable starts play synchronously in the gesture and same group never restarts', async () => {
  const Environment = mfEnvironment();
  const Controller = Environment.Controller;
  const Enabled = Controller.setEnabled(true);
  assert.equal(Environment.Audios[0].PlayCount, 1);
  await Enabled;
  assert.equal(Controller.getSnapshot().status, 'playing');
  Environment.Audios[0].currentTime = 32;
  Controller.setWeather('秋高气爽');
  Controller.setWeather('秋高气爽');
  await mfFlush();
  assert.equal(Environment.Audios.length, 1);
  assert.equal(Environment.Audios[0].currentTime, 32);
  assert.equal(Environment.Audios[0].PlayCount, 1);
  Controller.dispose();
});

await mfCheck('crossfade has bounded media and releases the previous source at completion', async () => {
  const Environment = mfEnvironment();
  const Controller = Environment.Controller;
  await Controller.setEnabled(true);
  const Sunny = Environment.Audios[0];
  Controller.setWeather('烟雨绵延');
  await mfFlush();
  const Rain = Environment.Audios[1];
  assert.equal(Controller.getSnapshot().trackId, 'rain');
  assert.equal(Sunny.paused, false);
  assert.equal(Rain.volume, 0);
  await Environment.Advance(2000);
  assert.ok(Rain.volume > 0 && Rain.volume < 0.25);
  assert.ok(Sunny.volume > 0 && Sunny.volume < 0.25);
  await Environment.Advance(2000);
  assert.equal(Sunny.paused, true);
  assert.equal(Sunny.src, '');
  assert.equal(Rain.volume, 0.25);
  assert.equal(Environment.Frames.size, 0);
  Controller.dispose();
});

await mfCheck('rapid switch discards stale play resolution instead of reviving its track', async () => {
  const Environment = mfEnvironment();
  const Controller = Environment.Controller;
  await Controller.setEnabled(true);
  const PendingRain = mfDeferred();
  Environment.PlayOutcomes.push(PendingRain.PromiseValue);
  Controller.setWeather('烟雨绵延');
  const Rain = Environment.Audios[1];
  Controller.setWeather('朔风凛冽');
  await mfFlush();
  assert.ok(Environment.Audios.filter(AudioValue => AudioValue.src).length <= 2);
  PendingRain.Resolve();
  await mfFlush();
  assert.equal(Rain.paused, true);
  assert.equal(Rain.src, '');
  assert.equal(Controller.getSnapshot().trackId, 'wind');
  assert.equal(Controller.getSnapshot().status, 'playing');
  await Controller.setEnabled(false);
  assert.ok(Environment.Audios.every(AudioValue => AudioValue.paused && !AudioValue.src));
  assert.equal(Environment.Frames.size, 0);
  Controller.dispose();
});

await mfCheck('disable during pending play remains off when play later resolves', async () => {
  const Environment = mfEnvironment();
  const Deferred = mfDeferred();
  Environment.PlayOutcomes.push(Deferred.PromiseValue);
  const Started = Environment.Controller.setEnabled(true);
  await Environment.Controller.setEnabled(false);
  Deferred.Resolve();
  await Started;
  assert.equal(Environment.Controller.getSnapshot().status, 'off');
  assert.equal(Environment.Audios[0].paused, true);
  assert.equal(Environment.Audios[0].src, '');
  Environment.Controller.dispose();
});

await mfCheck('autoplay rejection waits safely and the next user unlock retries', async () => {
  const Environment = mfEnvironment();
  const Blocked = new Error('gesture required');
  Blocked.name = 'NotAllowedError';
  Environment.PlayOutcomes.push(Blocked);
  await Environment.Controller.setEnabled(true);
  assert.equal(Environment.Controller.getSnapshot().status, 'waiting');
  await Environment.Controller.unlock();
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  Environment.Controller.dispose();
});

await mfCheck('failed target preserves an audible previous track and same action can retry', async () => {
  const Environment = mfEnvironment();
  const Controller = Environment.Controller;
  await Controller.setEnabled(true);
  const Sunny = Environment.Audios[0];
  Environment.PlayOutcomes.push(new Error('cannot decode'));
  Controller.setWeather('烟雨绵延');
  await mfFlush();
  assert.equal(Sunny.paused, false);
  assert.ok(Controller.getSnapshot().error);
  assert.equal(Environment.Audios[1].src, '');
  Controller.setMode('auto');
  await mfFlush();
  assert.equal(Controller.getSnapshot().status, 'playing');
  assert.equal(Controller.getSnapshot().error, '');
  assert.equal(Controller.getSnapshot().trackId, 'rain');
  Controller.dispose();
});

await mfCheck('element load errors release failed media and allow explicit retry', async () => {
  const Environment = mfEnvironment();
  await Environment.Controller.setEnabled(true);
  Environment.Audios[0].dispatchEvent(new Event('error'));
  assert.equal(Environment.Controller.getSnapshot().status, 'error');
  assert.equal(Environment.Audios[0].src, '');
  await Environment.Controller.unlock();
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  Environment.Controller.dispose();
});

await mfCheck('zero volume persists, manual whitelist overrides weather, invalid values are rejected', async () => {
  const Environment = mfEnvironment();
  const Controller = Environment.Controller;
  Controller.setVolume(0);
  Controller.setMode('mist');
  Controller.setWeather('丹霞映天');
  assert.equal(Controller.getSnapshot().trackId, 'mist');
  Controller.setMode('https://untrusted/audio.mp3');
  Controller.setVolume(NaN);
  assert.equal(Controller.getSnapshot().mode, 'mist');
  assert.equal(Controller.getSnapshot().volume, 0);
  await Controller.setEnabled(true);
  assert.equal(Environment.Audios[0].volume, 0);
  assert.equal(JSON.parse(Environment.Saved).volume, 0);
  Controller.setVolume(8);
  assert.equal(Controller.getSnapshot().volume, 1);
  Controller.dispose();
});

await mfCheck('saved enabled preference waits for a user gesture without eager download', async () => {
  const Environment = mfEnvironment(JSON.stringify({ version: 1, enabled: true, volume: 0, mode: 'rain' }));
  assert.equal(Environment.Controller.getSnapshot().status, 'waiting');
  assert.equal(Environment.Audios.length, 0);
  Environment.Document.dispatchEvent(new Event('pointerdown'));
  await mfFlush();
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  assert.equal(Environment.Audios[0].volume, 0);
  await Environment.Controller.setEnabled(false);
  Environment.Document.dispatchEvent(new Event('keydown'));
  await mfFlush();
  assert.equal(Environment.Controller.getSnapshot().status, 'off');
  Environment.Controller.dispose();
});

await mfCheck('malformed or inaccessible storage cannot prevent playing', async () => {
  for (const Saved of ['{bad', JSON.stringify({ version: 99, enabled: true, mode: 'rain' }), JSON.stringify({ version: 1, enabled: 'yes', volume: '0', mode: '__proto__' })]) {
    const Environment = mfEnvironment(Saved);
    assert.equal(Environment.Controller.getSnapshot().enabled, false);
    assert.equal(Environment.Controller.getSnapshot().volume, 0.25);
    assert.equal(Environment.Controller.getSnapshot().mode, 'auto');
    Environment.Controller.dispose();
  }
  const Environment = mfEnvironment(null, { Storage: { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); } } });
  await Environment.Controller.setEnabled(true);
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  Environment.Controller.dispose();
});

await mfCheck('hidden document pauses and visible resumes the same playback position', async () => {
  const Environment = mfEnvironment();
  const Controller = Environment.Controller;
  await Controller.setEnabled(true);
  const AudioValue = Environment.Audios[0];
  AudioValue.currentTime = 29;
  Environment.Document.hidden = true;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  assert.equal(Controller.getSnapshot().status, 'paused');
  assert.equal(AudioValue.paused, true);
  assert.equal(Environment.Frames.size, 0);
  Environment.Document.hidden = false;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  await mfFlush();
  assert.equal(Controller.getSnapshot().status, 'playing');
  assert.equal(Environment.Audios.length, 1);
  assert.equal(AudioValue.currentTime, 29);
  Controller.dispose();
  assert.equal(AudioValue.src, '');
  Environment.Document.dispatchEvent(new Event('pointerdown'));
  await mfFlush();
  assert.equal(Environment.Audios.length, 1);
});

await mfCheck('WebAudio unlock is synchronous, gain controls volume and resources disconnect', async () => {
  let ResumeCount = 0;
  let Closed = false;
  const Nodes = [];
  const Environment = mfEnvironment(null, { ContextFactory: () => ({
    state: 'suspended', destination: {},
    resume() { ResumeCount += 1; this.state = 'running'; return Promise.resolve(); },
    createMediaElementSource() { const Node = { connect() {}, disconnect() { this.Disconnected = true; } }; Nodes.push(Node); return Node; },
    createGain() { const Node = { gain: { value: 1 }, connect() {}, disconnect() { this.Disconnected = true; } }; Nodes.push(Node); return Node; },
    close() { Closed = true; return Promise.resolve(); },
  }) });
  const Started = Environment.Controller.setEnabled(true);
  assert.equal(ResumeCount, 1);
  assert.equal(Environment.Audios[0].PlayCount, 1);
  await Started;
  Environment.Controller.setVolume(0.4);
  assert.equal(Nodes.find(Node => Node.gain).gain.value, 0.4);
  assert.equal(Environment.Audios[0].volume, 1);
  Environment.Controller.dispose();
  assert.equal(Closed, true);
  assert.ok(Nodes.every(Node => Node.Disconnected));
});

await mfCheck('subscriptions unsubscribe and cannot mutate internal snapshots', async () => {
  const Environment = mfEnvironment();
  const Seen = [];
  const Unsubscribe = Environment.Controller.subscribe(Snapshot => Seen.push(Snapshot.trackId));
  const Snapshot = Environment.Controller.getSnapshot();
  assert.throws(() => { Snapshot.enabled = true; }, TypeError);
  Environment.Controller.setWeather('薄雾锁江');
  assert.equal(Seen.at(-1), 'mist');
  Unsubscribe();
  const Count = Seen.length;
  Environment.Controller.setWeather('丹霞映天');
  assert.equal(Seen.length, Count);
  Environment.Controller.dispose();
});

await mfCheck('repeated repaint during pending resume does not start playback twice', async () => {
  const Environment = mfEnvironment();
  await Environment.Controller.setEnabled(true);
  Environment.Document.hidden = true;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  const Deferred = mfDeferred();
  Environment.PlayOutcomes.push(Deferred.PromiseValue);
  Environment.Document.hidden = false;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  Environment.Controller.setWeather('秋高气爽');
  Environment.Controller.setWeather('秋高气爽');
  assert.equal(Environment.Audios[0].PlayCount, 2);
  Deferred.Resolve();
  await mfFlush();
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  assert.equal(Environment.Audios[0].src, '/assets/music/sunny.mp3');
  Environment.Controller.dispose();
});

await mfCheck('hiding again during a pending resume preserves the current source', async () => {
  const Environment = mfEnvironment();
  await Environment.Controller.setEnabled(true);
  Environment.Audios[0].currentTime = 37;
  Environment.Document.hidden = true;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  const Deferred = mfDeferred();
  Environment.PlayOutcomes.push(Deferred.PromiseValue);
  Environment.Document.hidden = false;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  Environment.Document.hidden = true;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  Deferred.Resolve();
  await mfFlush();
  assert.equal(Environment.Audios[0].paused, true);
  assert.equal(Environment.Audios[0].src, '/assets/music/sunny.mp3');
  assert.equal(Environment.Controller.getSnapshot().status, 'paused');
  Environment.Document.hidden = false;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  await mfFlush();
  assert.equal(Environment.Audios[0].currentTime, 37);
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  Environment.Controller.dispose();
});

await mfCheck('enabling in a hidden document publishes paused without downloading media', async () => {
  const Environment = mfEnvironment();
  Environment.Document.hidden = true;
  await Environment.Controller.setEnabled(true);
  assert.equal(Environment.Controller.getSnapshot().status, 'paused');
  assert.equal(Environment.Audios.length, 0);
  Environment.Document.hidden = false;
  Environment.Document.dispatchEvent(new Event('visibilitychange'));
  await mfFlush();
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  Environment.Controller.dispose();
});

await mfCheck('returning to the preserved playable song clears a previous target failure', async () => {
  const Environment = mfEnvironment();
  await Environment.Controller.setEnabled(true);
  Environment.PlayOutcomes.push(new Error('failed rain download'));
  Environment.Controller.setWeather('烟雨绵延');
  await mfFlush();
  assert.ok(Environment.Controller.getSnapshot().error);
  Environment.Controller.setWeather('春和景明');
  await mfFlush();
  assert.equal(Environment.Controller.getSnapshot().error, '');
  assert.equal(Environment.Controller.getSnapshot().status, 'playing');
  assert.equal(Environment.Audios[0].PlayCount, 1);
  Environment.Controller.dispose();
});

console.log(`Weather music: ${Checks} behavioral groups passed.`);
