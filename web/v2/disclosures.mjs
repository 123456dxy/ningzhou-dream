// Preserve reading/work positions across authoritative state and catalogue updates.
// Explicit keys win; fallback uses nearby headings plus summary, never item values.
export function detailsKeys(root,scope=''){
  const counts=new Map();
  return [...root.querySelectorAll('details')].map(detail=>{
    if(detail.dataset.disclosureKey)return [detail,`${scope}|${detail.dataset.disclosureKey}`];
    const summary=detail.querySelector(':scope > summary')?.textContent?.trim()||'';
    const headings=[];let parent=detail.parentElement;
    while(parent&&parent!==root){const h=parent.querySelector(':scope > h1,:scope > h2,:scope > h3,:scope > .row > strong,:scope > .row > h3');if(h)headings.unshift(h.textContent.trim());parent=parent.parentElement;}
    const base=`${scope}|${headings.join('>')}|${summary.replace(/\d+/g,'#')}`,index=counts.get(base)||0;counts.set(base,index+1);return [detail,`${base}|${index}`];
  });
}
export function captureDisclosures(root,scope,memory){for(const [node,key]of detailsKeys(root,scope))memory.set(key,node.open);}
export function restoreDisclosures(root,scope,memory){for(const [node,key]of detailsKeys(root,scope))if(memory.has(key))node.open=memory.get(key);}
