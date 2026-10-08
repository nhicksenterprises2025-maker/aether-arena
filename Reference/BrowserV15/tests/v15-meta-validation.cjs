const fs=require('node:fs');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const {cards,rules,styles,root}=require('./meta-fixtures.cjs');
(async()=>{
  const {createMetaEngine}=await import(pathToFileURL(path.join(root,'src/meta-engine.js')));
  const engine=createMetaEngine(cards,{rules,styles,version:'V15',seed:151515,workerUsage:'Node command-line event engine; browser worker validated separately'}),count=Number(process.argv[2]||10000),started=performance.now();
  engine.runBatch(count);
  const view=engine.view(),report={...engine.validation(),wallMilliseconds:performance.now()-started,seed:151515,rows:view.cards,styles:view.personalities.map(s=>({style:s.style,n:s.n})),archetypes:view.archetypes.map(a=>({name:a.name,n:a.n})),pairs:view.synergies.length,historyCheckpoints:view.history.length,serializedDatasetBytes:Buffer.byteLength(JSON.stringify(engine.snapshot())),alerts:view.alerts.map(({reasons,...summary})=>summary)};
  if(!report.sanity||report.games!==count||report.rows.some(r=>r.cleanN===0))throw new Error('Meta validation invariants failed.');
  const output=path.join(root,'V15_META_VALIDATION.json');fs.writeFileSync(output,JSON.stringify(report,null,2));
  console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
