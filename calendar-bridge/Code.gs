/** Weather Display calendar bridge. Read-only Calendar API access only.
 * Run setup() once; deploy as web app executing as you, accessible to Anyone.
 * The separate, random DISPLAY_TOKEN protects event access.
 */
function setup() {
  const p = PropertiesService.getScriptProperties();
  if (!p.getProperty('DISPLAY_TOKEN')) p.setProperty('DISPLAY_TOKEN', Utilities.getUuid().replace(/-/g,'') + Utilities.getUuid().replace(/-/g,''));
  if (!p.getProperty('CALENDAR_IDS')) p.setProperty('CALENDAR_IDS', 'primary');
  // Manual setup only: copy this token locally to the touchscreen. Never send it in chat.
  console.log('Display token: ' + p.getProperty('DISPLAY_TOKEN'));
  console.log('Calendars: ' + p.getProperty('CALENDAR_IDS'));
  Calendar.Events.list('primary', {maxResults: 1}); // Trigger the read-only consent flow.
}
function output_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
function equalToken_(a,b) {
  if (!a || !b || a.length !== b.length) return false;
  let diff = 0; for (let i=0;i<a.length;i++) diff |= a.charCodeAt(i)^b.charCodeAt(i);
  return diff === 0;
}
function doGet(e) {
  const p=PropertiesService.getScriptProperties(), args=(e && e.parameter)||{};
  if (!equalToken_(args.token, p.getProperty('DISPLAY_TOKEN'))) return output_({error:'Calendar token rejected'});
  const from=Number(args.from), to=Number(args.to), now=Date.now()/1000;
  if (!Number.isFinite(from) || !Number.isFinite(to) || to<=from || to-from>8*86400 || Math.abs(from-now)>100*86400)
    return output_({error:'Invalid calendar range'});
  try {
    const ids=(p.getProperty('CALENDAR_IDS')||'primary').split(',').map(s=>s.trim()).filter(Boolean).slice(0,8);
    const colors=Calendar.Colors.get();
    let events=[], truncated=false;
    ids.forEach(id=>{
      const cal=Calendar.CalendarList.get(id);
      let pageToken;
      // Fetch bounded pages of expanded recurring instances, including events crossing range boundaries.
      let pages=0;
      do {
        const options={timeMin:new Date(from*1000).toISOString(),timeMax:new Date(to*1000).toISOString(),singleEvents:true,orderBy:'startTime',maxResults:100};
        if(pageToken)options.pageToken=pageToken;
        const result=Calendar.Events.list(id,options);
        (result.items||[]).forEach(ev=>{
          if(ev.status==='cancelled')return;
          const allDay=Boolean(ev.start && ev.start.date);
          if(!ev.start || !ev.end)return;
          const color=ev.colorId && colors.event && colors.event[ev.colorId] ? colors.event[ev.colorId].background : (cal.backgroundColor || '#4285f4');
          events.push({title:String(ev.summary||'(Untitled)').slice(0,100),
            location:String(ev.location||'').slice(0,160),
            description:String(ev.description||'').replace(/<[^>]*>/g,' ').slice(0,320),
            calendar:String(cal.summary||id).slice(0,80),color:color,allDay:allDay,
            start:allDay?0:Math.floor(Date.parse(ev.start.dateTime)/1000),
            end:allDay?0:Math.floor(Date.parse(ev.end.dateTime)/1000),
            startDate:allDay?ev.start.date:'',endDate:allDay?ev.end.date:''});
        });
        pageToken=result.nextPageToken;pages++;
      } while(pageToken && pages<2);
      if(pageToken)truncated=true;
    });
    // Sort across calendars before applying the device's cap. All-day date strings sort at midnight UTC for stable ordering.
    const key=e=>e.allDay?Date.parse(e.startDate+'T00:00:00Z')/1000:e.start;
    events.sort((a,b)=>key(a)-key(b));
    if(events.length>100)truncated=true;
    const payload={version:1,generated:Math.floor(Date.now()/1000),from:from,to:to,events:events.slice(0,100),truncated:truncated};
    // Keep UTF-8 response bytes below the device's 100 KB parser limit, even for Unicode titles.
    const bytes=s=>encodeURIComponent(s).replace(/%[A-F0-9]{2}/g,'x').length;
    while(payload.events.length && bytes(JSON.stringify(payload))>90000){payload.events.pop();payload.truncated=true;}
    return output_(payload);
  } catch(err) {
    // No tokens, request URLs, or private event data are written to logs.
    return output_({error:'Calendar read failed. Check calendar IDs, read permission, and API service setup.'});
  }
}
