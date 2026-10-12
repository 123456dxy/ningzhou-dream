// One authoritative server state. View preferences never contain game rules.
export function createStore() {
  let value = {state:null,account:null,revision:0,report:null,outcome:null,settlement:null,data:{},ui:{route:'home',mapTab:'places',inventoryTab:'bag',artsTab:'loadout',social:'friends'}};
  const listeners = new Set();
  return {
    get: () => value,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    patch(patch, notify = true) { value = {...value,...patch}; if (notify) listeners.forEach(fn => fn(value)); },
    ui(patch) { this.patch({ui:{...value.ui,...patch}}); },
    data(patch) { this.patch({data:{...value.data,...patch}}); },
    accept(result) {
      const changed = Object.hasOwn(result, 'state') || result.hasGame === false;
      this.patch({...(changed ? {state:result.state || null,revision:value.revision+1,data:{}} : {}),report:result.report || null});
    },
    reset(account) { this.patch({state:null,account,revision:value.revision+1,data:{},report:null,outcome:null,settlement:null,ui:{route:'home',mapTab:'places',inventoryTab:'bag',artsTab:'loadout',social:'friends'}}); }
  };
}
