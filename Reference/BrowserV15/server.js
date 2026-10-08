const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');

const root = __dirname;
const port = Number(process.env.PORT || 8080);
const saveDir = process.env.LOCALAPPDATA ? path.join(process.env.LOCALAPPDATA,'RiftCrownArena') : path.join(os.homedir(),'.riftcrownarena');
fs.mkdirSync(saveDir,{recursive:true});
const saveFile = path.join(saveDir,'player_save.json');
const saveTempFile = `${saveFile}.${process.pid}.tmp`;
const maxSaveBytes = 262144;
const labFile = path.join(saveDir,'lab_v15.json');
const maxLabBytes = 16777216;
const types = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8', '.glb': 'model/gltf-binary', '.gltf': 'model/gltf+json', '.png': 'image/png', '.svg': 'image/svg+xml'
};
function send(res,status,type,data){res.writeHead(status,{'Content-Type':type,'Cache-Control':'no-store','Access-Control-Allow-Origin':'*'});res.end(data);}
http.createServer((req, res) => {
  let clean;
  try { clean = decodeURIComponent(req.url.split('?')[0]); }
  catch { return send(res,400,'text/plain; charset=utf-8','Bad request'); }
  if(clean==='/api/save'||clean==='/api/lab'){
    const apiFile=clean==='/api/lab'?labFile:saveFile;
    const apiTemp=clean==='/api/lab'?`${labFile}.${process.pid}.tmp`:saveTempFile;
    const maxBytes=clean==='/api/lab'?maxLabBytes:maxSaveBytes;
    if(req.method==='GET'){
      fs.readFile(apiFile,(err,data)=>send(res,200,'application/json; charset=utf-8',err?Buffer.from('{}'):data));
      return;
    }
    if(req.method==='POST'){
      const chunks=[];
      let bytes=0,tooLarge=false;
      req.on('data',chunk=>{
        bytes+=chunk.length;
        if(bytes>maxBytes){
          if(!tooLarge){tooLarge=true;chunks.length=0;send(res,413,'application/json; charset=utf-8','{"ok":false}');}
          return;
        }
        chunks.push(chunk);
      });
      req.on('end',()=>{
        if(tooLarge)return;
        const body=Buffer.concat(chunks).toString('utf8');
        try { JSON.parse(body); }
        catch { return send(res,400,'application/json; charset=utf-8','{"ok":false}'); }
        try {
          fs.writeFileSync(apiTemp,body,'utf8');
          fs.renameSync(apiTemp,apiFile);
          send(res,200,'application/json; charset=utf-8','{"ok":true}');
        } catch {
          try { fs.unlinkSync(apiTemp); } catch {}
          send(res,500,'application/json; charset=utf-8','{"ok":false}');
        }
      });
      return;
    }
    return send(res,405,'text/plain; charset=utf-8','Method not allowed');
  }
  if(req.method!=='GET')return send(res,405,'text/plain; charset=utf-8','Method not allowed');
  let urlPath=clean==='/'?'/index.html':clean;
  const file=path.resolve(root,'.'+urlPath);
  const rel=path.relative(root,file);
  if(rel.startsWith('..')||path.isAbsolute(rel))return send(res,403,'text/plain; charset=utf-8','Forbidden');
  fs.readFile(file,(err,data)=>{if(err)return send(res,404,'text/plain; charset=utf-8','Not found');send(res,200,types[path.extname(file)]||'application/octet-stream',data);});
}).listen(port,()=>console.log(`Rift Crown Arena running at http://localhost:${port}`));
