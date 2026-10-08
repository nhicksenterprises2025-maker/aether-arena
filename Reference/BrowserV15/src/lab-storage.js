const KEY='rift_crown_lab_v15',PENDING='rift_crown_lab_pending_v15';
export function createLabPersistence({storage=globalThis.localStorage,fetcher=globalThis.fetch}={}){
  let pending=Promise.resolve(),revision=0;
  const read=key=>{try{return storage.getItem(key);}catch{return null;}};
  const write=(key,value)=>{try{storage.setItem(key,value);}catch{}};
  const normalize=value=>value?.schema===1&&value.meta?.schema===2&&(Array.isArray(value.replays)||Array.isArray(value.replays?.replays))?{...value,replays:Array.isArray(value.replays)?value.replays:value.replays.replays}:null;
  async function request(method,body){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),5000);try{return await fetcher('/api/lab',{method,headers:body?{'Content-Type':'application/json'}:undefined,body,signal:controller.signal,cache:'no-store'});}finally{clearTimeout(timer);}}
  async function load(){
    let local=null;try{local=JSON.parse(read(KEY)||'null');}catch{}
    const before=revision,hasPending=!!read(PENDING);
    try{const response=await request('GET');if(response.ok){const remote=await response.json();const clean=normalize(remote);if(clean&&!hasPending&&before===revision){write(KEY,JSON.stringify(clean));return clean;}}}catch{}
    return normalize(local);
  }
  function save(payload){
    const token=`${Date.now()}-${++revision}`,body=JSON.stringify({schema:1,...payload,savedAt:new Date().toISOString()});
    write(KEY,body);write(PENDING,token);
    pending=pending.catch(()=>{}).then(async()=>{try{const response=await request('POST',body);if(response.ok&&read(PENDING)===token){try{storage.removeItem(PENDING);}catch{}}return response.ok;}catch{return false;}});
    return pending;
  }
  return{load,save,flush:()=>pending};
}
