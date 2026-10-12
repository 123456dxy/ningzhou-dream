// Same-origin transport; one mutation at a time across ALL gameplay modes.
// Only Tianji supports request-id replay. Ordinary mutations are never auto-retried.
export class ApiError extends Error {
  constructor(message, status = 0, result = null, uncertain = false) { super(message); this.status=status;this.result=result;this.uncertain=uncertain; }
}
export function createApi({fetcher = globalThis.fetch.bind(globalThis), onBusy = () => {}, timeout = 25000, storage} = {}) {
  let busy = false, pendingByAccount = Object.create(null);
  try {
    storage ||= globalThis.sessionStorage;
    const parsed=JSON.parse(storage?.getItem('nz-v2-tianji-pending-v2') || '{}');
    if(parsed && typeof parsed === 'object' && !Array.isArray(parsed)) for(const [key,value] of Object.entries(parsed)) {
      if(value && value.account===key && ['tianji/join','tianji/challenge','tianji/action'].includes(value.path) && value.body && typeof value.body === 'object' && typeof value.body.requestId === 'string') pendingByAccount[key]=value;
    }
  } catch { /* storage is optional */ }
  const savePending = (value, account) => { if(value)pendingByAccount[account]=value;else delete pendingByAccount[account];try {storage?.setItem('nz-v2-tianji-pending-v2',JSON.stringify(pendingByAccount));}catch{} };
  async function request(path, body, method = 'POST') {
    if (!/^[a-z0-9][a-z0-9/?=&%_-]*$/i.test(path) || path.includes('..')) throw new ApiError('无效的接口地址');
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(),timeout);
    try {
      const res = await fetcher('/api/' + path, {method,credentials:'same-origin',cache:'no-store',signal:controller.signal,...(method !== 'GET' ? {headers:{'Content-Type':'application/json'},body:JSON.stringify(body || {})} : {})});
      const result = await res.json().catch(() => { throw new ApiError('服务返回的内容无法读取，请刷新确认当前进度。',res.status,null,true); });
      if(!res.ok || result.ok === false || result.error && result.valid == null) throw new ApiError(result.error || `请求失败（${res.status}）`,res.status,result,res.status >= 500);
      return result;
    } catch(error) {
      if(error instanceof ApiError) throw error;
      throw new ApiError(error.name === 'AbortError' ? '等待服务超时。请先刷新确认结果，避免重复操作。' : '未能连接游戏服务。请检查网络与服务状态。',0,null,true);
    } finally { clearTimeout(timer); }
  }
  return {
    read:(path,body = {},method = 'POST') => request(path,body,method),
    get busy(){return busy;}, pendingFor(account){return pendingByAccount[account] || null;},
    clearPending(account){savePending(null,account);},
    async mutate(path, body = {}, {retry = false, account = ''} = {}) {
      if(busy) throw new ApiError('上一项操作仍在处理中，请稍候。',409);
      const isTianji = path.startsWith('tianji/');
      const pending=pendingByAccount[account];
      if(pending && !retry) throw new ApiError('天机操作的结果尚未确认，请先核对或重试该操作。',409);
      let command = {path,body,account};
      if(retry) {
        if(!pending || pending.account !== account) throw new ApiError('此待确认操作不属于当前道号。请切回原道号后核对。',409);
        command = pending;
      } else if(isTianji) {
        command.body = {...body,requestId:globalThis.crypto?.randomUUID?.() || `v2-${Date.now()}-${Math.random().toString(36).slice(2)}`};
      }
      busy=true;onBusy(true);
      if(isTianji || retry) savePending(command,account);
      try { const result=await request(command.path,command.body);if(isTianji || retry)savePending(null,account);return result; }
      catch(e) { if((isTianji || retry) && !e.uncertain && e.status !== 401)savePending(null,account);throw e; }
      finally { busy=false;onBusy(false); }
    }
  };
}
