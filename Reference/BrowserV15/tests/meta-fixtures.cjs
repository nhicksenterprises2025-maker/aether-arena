const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const root=process.env.RIFT_PROJECT_ROOT||path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'src/game.js'),'utf8');
function constant(name){const start=source.indexOf(`const ${name} =`);if(start<0)throw new Error(`Missing ${name}`);const end=source.indexOf('\n};',start)+3;if(name==='CARD_LIBRARY'||name==='AI_STYLES')return source.slice(start,end);return source.slice(start,source.indexOf(';',start)+1);}
const context={};vm.createContext(context);vm.runInContext([constant('CARD_LIBRARY'),constant('AI_STYLES'),constant('MATCH'),constant('ARENA'),constant('SIGHT'),constant('RETARGET'),constant('GRID'),'this.cards=CARD_LIBRARY;this.styles=AI_STYLES;this.rules={match:MATCH,arena:ARENA,sight:SIGHT,retarget:RETARGET,grid:GRID,towers:{guard:{hp:2250,damage:86,attackSpeed:1.02,range:10,radius:1.15},core:{hp:3600,damage:112,attackSpeed:.92,range:8.9,radius:1.35}}};'].join('\n'),context);
module.exports={cards:JSON.parse(JSON.stringify(context.cards)),styles:JSON.parse(JSON.stringify(context.styles)),rules:JSON.parse(JSON.stringify(context.rules)),root};
