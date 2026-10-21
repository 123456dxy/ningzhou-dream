import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { randomUUID } from 'node:crypto';
import { mfCreateRun, mfGetView, mfAct, mfQuote } from './engine.mjs';

const Directory = dirname(fileURLToPath(import.meta.url));
const Sessions = new Map();
const Arguments = process.argv.slice(2);
const PortIndex = Arguments.indexOf('--port');
const Port = PortIndex >= 0 ? Number(Arguments[PortIndex + 1]) : 0;
if (!Number.isInteger(Port) || Port < 0 || Port > 65535) throw new Error('Invalid loop prototype port.');

function mfSend(Response, Status, Value) {
    Response.writeHead(Status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
    Response.end(JSON.stringify(Value));
}

async function mfReadBody(Request) {
    let Body = '';
    for await (const Chunk of Request) {
        Body += Chunk.toString();
        if (Body.length > 32768) throw new Error('请求内容过大。');
    }
    const Value = JSON.parse(Body || '{}');
    if (!Value || Array.isArray(Value) || typeof Value !== 'object') throw new Error('请求格式无效。');
    return Value;
}

function mfSnapshot(Session) { return { sessionId: Session.Id, view: mfGetView(Session.Run) }; }

const Server = createServer(async (Request, Response) => {
    try {
        const Url = new URL(Request.url, 'http://127.0.0.1');
        if (Url.pathname.startsWith('/api/')) {
            const Method = ['/api/health', '/api/state'].includes(Url.pathname) ? 'GET' : 'POST';
            if (Request.method !== Method) return mfSend(Response, 405, { error: '请求方法不支持。' });
            const Origin = Request.headers.origin;
            if (Origin && Origin !== `http://${Request.headers.host}`) return mfSend(Response, 403, { error: '仅接受本地试玩页面请求。' });
            if (Url.pathname === '/api/health') return mfSend(Response, 200, { ok: true, isolated: true, prototype: 'meridian-loop' });
            const Body = Request.method === 'POST' ? await mfReadBody(Request) : {};
            if (Url.pathname === '/api/session') {
                if (Object.keys(Body).length) return mfSend(Response, 400, { error: '新会话不支持额外参数。' });
                if (Sessions.size >= 256) return mfSend(Response, 503, { error: '试玩会话已满，请重启沙盘。' });
                const Session = { Id: randomUUID(), Run: mfCreateRun(), Receipts: new Map() };
                Sessions.set(Session.Id, Session);
                return mfSend(Response, 200, mfSnapshot(Session));
            }
            const Session = Sessions.get(Request.headers['x-loop-session']);
            if (!Session) return mfSend(Response, 401, { error: '试玩会话已失效，请重新连接。' });
            if (Url.pathname === '/api/state') return mfSend(Response, 200, mfSnapshot(Session));
            if (!['/api/quote', '/api/action', '/api/start'].includes(Url.pathname)) return mfSend(Response, 404, { error: '未知接口。' });
            const AllowedKeys = Url.pathname === '/api/quote' ? ['revision', 'action']
                : Url.pathname === '/api/start' ? ['revision', 'requestId', 'settings'] : ['revision', 'requestId', 'action'];
            if (Object.keys(Body).some(Key => !AllowedKeys.includes(Key))) return mfSend(Response, 400, { error: '请求包含未知字段。' });
            if (!Number.isInteger(Body.revision) || Body.revision < 0) return mfSend(Response, 400, { error: '缺少有效局面版本。' });
            if (Url.pathname === '/api/quote') {
                if (Body.revision !== Session.Run.revision) return mfSend(Response, 409, { error: '局面已变化，已刷新。', ...mfSnapshot(Session) });
                return mfSend(Response, 200, { quote: mfQuote(Session.Run, Body.action), revision: Session.Run.revision });
            }
            if (typeof Body.requestId !== 'string' || !Body.requestId.length || Body.requestId.length > 100) return mfSend(Response, 400, { error: '缺少动作标识。' });
            const Signature = JSON.stringify({ path: Url.pathname, revision: Body.revision, action: Body.action, settings: Body.settings });
            if (Session.Receipts.has(Body.requestId)) {
                if (Session.Receipts.get(Body.requestId) !== Signature) return mfSend(Response, 409, { error: '重复动作内容不一致。', ...mfSnapshot(Session) });
                return mfSend(Response, 200, { ...mfSnapshot(Session), replayed: true });
            }
            if (Body.revision !== Session.Run.revision) return mfSend(Response, 409, { error: '局面已变化，已刷新。', ...mfSnapshot(Session) });
            const Candidate = Url.pathname === '/api/start' ? mfCreateRun(Body.settings) : mfAct(Session.Run, Body.action);
            if (Url.pathname === '/api/start') Candidate.revision = Session.Run.revision + 1;
            Session.Run = Candidate;
            Session.Receipts.set(Body.requestId, Signature);
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
    } catch (Failure) { mfSend(Response, 400, { error: Failure.message || '本次操作未提交，请重试。' }); }
});

Server.listen(Port, '127.0.0.1', () => process.stdout.write(`周天搭建沙盘已启动 http://127.0.0.1:${Server.address().port}/\n`));
