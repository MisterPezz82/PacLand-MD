// Portable builds use a loopback HTTP compiler server; no external services.
import net from 'node:net';
const connect=net.Socket.prototype.connect;
net.Socket.prototype.connect=function(...args){
 const a=Array.isArray(args[0])?args[0]:args,first=a[0];
 let host;
 if(first&&typeof first==='object'){
  if(first.path){if(String(first.path).startsWith('\\\\')&&!String(first.path).startsWith('\\\\.\\pipe\\'))throw Error('External pipe refused');return connect.apply(this,args);}
  host=first.host??first.hostname??'localhost';
 }else if(typeof first==='string'&&!/^\d+$/.test(first))return connect.apply(this,args);
 else host=typeof a[1]==='string'?a[1]:'localhost';
 host=String(host).toLowerCase();
 if(host!=='localhost'&&host!=='::1'&&!host.startsWith('127.')&&!host.startsWith('::ffff:127.'))throw Error('Portable ROM maker blocks external network access: '+host);
 return connect.apply(this,args);
};
