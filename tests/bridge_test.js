// No Google credentials required. Exercise the real Apps Script bridge with service mocks.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
let calls=0;const token='a'.repeat(64);
const properties={DISPLAY_TOKEN:token,CALENDAR_IDS:'primary,second'};
const sandbox={console,Date,Number,PropertiesService:{getScriptProperties:()=>({getProperty:k=>properties[k]})},ContentService:{MimeType:{JSON:'json'},createTextOutput:text=>({setMimeType:()=>JSON.parse(text)})},Calendar:{Colors:{get:()=>({event:{'2':{background:'#00aa00'}}})},CalendarList:{get:id=>({summary:id,backgroundColor:'#4285f4'})},Events:{list:(id,opts)=>{calls++;assert.strictEqual(opts.singleEvents,true);return {items:[{summary:id+' timed',start:{dateTime:'2026-11-10T09:00:00-06:00'},end:{dateTime:'2026-11-10T10:00:00-06:00'},colorId:'2'},{summary:id+' all-day',start:{date:'2026-11-10'},end:{date:'2026-11-12'}},{status:'cancelled',summary:'cancelled'}]};}}}};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('calendar-bridge/Code.gs','utf8'),sandbox);
const now=Math.floor(Date.now()/1000),from=now-86400,to=now+6*86400;
assert.strictEqual(sandbox.doGet({parameter:{token:'wrong',from,to}}).error,'Calendar token rejected');assert.strictEqual(calls,0);
assert.strictEqual(sandbox.doGet({parameter:{token,from,to:from+9*86400}}).error,'Invalid calendar range');assert.strictEqual(calls,0);
const result=sandbox.doGet({parameter:{token,from,to}});assert.strictEqual(result.events.length,4);assert.strictEqual(result.truncated,false);
const all=result.events.find(e=>e.allDay);assert.strictEqual(all.startDate,'2026-11-10');assert.strictEqual(all.endDate,'2026-11-12');
const timed=result.events.find(e=>!e.allDay);assert.strictEqual(timed.color,'#00aa00');assert.strictEqual(timed.end-timed.start,3600);
sandbox.Calendar.Events.list=()=>({items:Array.from({length:100},(_,i)=>({summary:'Event '+i,start:{dateTime:'2026-11-10T09:00:00Z'},end:{dateTime:'2026-11-10T10:00:00Z'}})),nextPageToken:'more'});
const capped=sandbox.doGet({parameter:{token,from,to}});assert.strictEqual(capped.events.length,100);assert.strictEqual(capped.truncated,true);
sandbox.Calendar.Events.list=()=>{throw Error('Private info must not escape');};
assert(!sandbox.doGet({parameter:{token,from,to}}).error.includes('Private'));
console.log('PASS: token gate, range limits, recurring expansion request, multiple calendars, colors, all-day dates, event cap, errors');
