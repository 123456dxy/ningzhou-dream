import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { randomInt, randomUUID } from 'node:crypto';
import { mfCreateRun, mfGetView, mfAct, mfQuote } from './engine.mjs';

const Directory = dirname(fileURLToPath(import.meta.url));
const Sessions = new Map();
const Modes = ['elements', 'meridians'];
const Arguments = process.argv.slice(2);
const PortIndex = Arguments.indexOf('--port');
const Port = PortIndex >= 0 ? Number(Arguments[PortIndex + 1]) : 0;

function mfSend(Response, Status, Body) {
    Response.writeHead(Status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
    Response.end(JSON.stringify(Body));
}

async function mfReadBody(Request) {
    let Body = '';
    for await (const Chunk of Request) {
        Body += Chunk.toString();
        if (Body.length > 16384) throw new Error('请求内容过大。');
    }
    const Value = JSON.parse(Body || '{}');
    if (!Value || Array.isArray(Value) || typeof Value !== 'object') throw new Error('请求格式无效。');
    return Value;
}

function mfSnapshot(Session) {
    return { sessionId: Session.Id, views: Object.fromEntries(Modes.map(Mode => [Mode,
        { ...mfGetView(Session.Runs[Mode]), revision: Session.Revisions[Mode] }])) };
}

function mfStartRun(Mode, Settings = {}) {
    const Validated = mfCreateRun(Mode, Settings);
    return mfCreateRun(Mode, Validated.settings, randomInt(0x100000000));
}

function mfCombinations(Values, Count, Start = 0, Pick = [], Result = []) {
    if (Pick.length === Count) { Result.push([...Pick]); return Result; }
    for (let Index = Start; Index <= Values.length - (Count - Pick.length); Index++) {
        if (Index > Start && Values[Index] === Values[Index - 1]) continue;
        Pick.push(Values[Index]);
        mfCombinations(Values, Count, Index + 1, Pick, Result);
        Pick.pop();
    }
    return Result;
}

// Suggestions only choose inputs. The same engine quote validates every candidate.
function mfResolveQuote(Run, Action, AutoPick) {
    if (!Action || typeof Action.kind !== 'string') throw new Error('请选择一种术式。');
    if (!AutoPick || Run.mode !== 'elements' || !['basic', 'same', 'generate', 'five', 'convert', 'gather'].includes(Action.kind)) {
        return { action: Action, quote: mfQuote(Run, Action) };
    }
    const View = mfGetView(Run);
    const Values = Object.entries(View.resources || {}).flatMap(([Element, Count]) => Array(Count).fill(Element)).sort();
    const Sizes = { basic: [2], same: [3], generate: [4], five: [5], convert: [1, 2], gather: [2] }[Action.kind];
    for (const Size of Sizes) {
        for (const Spend of mfCombinations(Values, Size)) {
            const Candidate = { ...Action, spend: Spend };
            const Quote = mfQuote(Run, Candidate);
            if (Quote.enabled) return { action: Candidate, quote: Quote };
        }
    }
    return { action: Action, quote: mfQuote(Run, Action) };
}

const Server = createServer(async (Request, Response) => {
    try {
        const Url = new URL(Request.url, 'http://127.0.0.1');
        if (Url.pathname === '/api/health') return mfSend(Response, 200, { ok: true, isolated: true });
        if (Url.pathname.startsWith('/api/')) {
            const Method = Url.pathname === '/api/state' ? 'GET' : 'POST';
            if (Request.method !== Method) return mfSend(Response, 405, { error: '请求方法不支持。' });
            const Body = Request.method === 'POST' ? await mfReadBody(Request) : {};
            const Origin = Request.headers.origin;
            if (Origin && Origin !== `http://${Request.headers.host}`) return mfSend(Response, 403, { error: '仅接受本地试玩页面请求。' });
            if (Url.pathname === '/api/session') {
                if (Sessions.size >= 256) return mfSend(Response, 503, { error: '试玩会话已满，请重启沙盘。' });
                const Session = { Id: randomUUID(), Runs: Object.fromEntries(Modes.map(Mode => [Mode, mfStartRun(Mode, Mode === 'elements' ? { tide: 'tidal' } : {})])),
                    Revisions: { elements: 0, meridians: 0 }, Receipts: new Map() };
                Sessions.set(Session.Id, Session);
                return mfSend(Response, 200, mfSnapshot(Session));
            }
            const Session = Sessions.get(Request.headers['x-lab-session']);
            if (!Session) return mfSend(Response, 401, { error: '试玩会话已失效，请重新连接。' });
            if (Url.pathname === '/api/state') return mfSend(Response, 200, mfSnapshot(Session));
            const Mode = Body.mode;
            if (!Modes.includes(Mode)) return mfSend(Response, 400, { error: '未知试玩方案。' });
            if (Url.pathname === '/api/quote') {
                if (Body.revision !== Session.Revisions[Mode]) return mfSend(Response, 409, { error: '局面已经变化，已为你刷新。', ...mfSnapshot(Session) });
                return mfSend(Response, 200, { ...mfResolveQuote(Session.Runs[Mode], Body.action, Body.autoPick === true), revision: Session.Revisions[Mode] });
            }
            if (!['/api/action', '/api/start'].includes(Url.pathname)) return mfSend(Response, 404, { error: '未知接口。' });
            if (typeof Body.requestId !== 'string' || Body.requestId.length > 100) return mfSend(Response, 400, { error: '缺少动作标识。' });
            const Key = `${Mode}:${Body.requestId}`;
            const Signature = JSON.stringify({ path: Url.pathname, action: Body.action, settings: Body.settings, revision: Body.revision });
            if (Session.Receipts.has(Key)) {
                if (Session.Receipts.get(Key) !== Signature) return mfSend(Response, 409, { error: '重复动作内容不一致。', ...mfSnapshot(Session) });
                return mfSend(Response, 200, { ...mfSnapshot(Session), replayed: true });
            }
            if (Body.revision !== Session.Revisions[Mode]) return mfSend(Response, 409, { error: '局面已经变化，已为你刷新。', ...mfSnapshot(Session) });
            const Candidate = Url.pathname === '/api/start' ? mfStartRun(Mode, Body.settings) : mfAct(Session.Runs[Mode], Body.action);
            Session.Runs[Mode] = Candidate;
            Session.Revisions[Mode]++;
            Session.Receipts.set(Key, Signature);
            if (Session.Receipts.size > 256) Session.Receipts.delete(Session.Receipts.keys().next().value);
            return mfSend(Response, 200, mfSnapshot(Session));
        }
        const Files = { '/': ['index.html', 'text/html'], '/index.html': ['index.html', 'text/html'],
            '/app.mjs': ['app.mjs', 'text/javascript'], '/styles.css': ['styles.css', 'text/css'] };
        if (Url.pathname === '/favicon.ico') { Response.writeHead(204); return Response.end(); }
        if (Request.method !== 'GET' || !Files[Url.pathname]) { Response.writeHead(404); return Response.end('Not found'); }
        const [File, Type] = Files[Url.pathname];
        const Content = await readFile(join(Directory, File));
        Response.writeHead(200, { 'Content-Type': `${Type}; charset=utf-8`, 'Cache-Control': 'no-store',
            'X-Content-Type-Options': 'nosniff', 'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'" });
        Response.end(Content);
    } catch (ErrorValue) {
        mfSend(Response, 400, { error: ErrorValue.message || '操作未完成，请重试。' });
    }
});

Server.listen(Port, '127.0.0.1', () => {
    process.stdout.write(`筑基沙盘已启动 http://127.0.0.1:${Server.address().port}/\n`);
});
