export const endpoint=process.env.ROMDEV_URL||'http://localhost:7332';
export async function call(name,args,session='pacland-md') {
 const r=await fetch(endpoint+'/tool/'+name,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':session},body:JSON.stringify(args)});
 const j=await r.json();if(!r.ok)throw Error(JSON.stringify(j));return j;
}
