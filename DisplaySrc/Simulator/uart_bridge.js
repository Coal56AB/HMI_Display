/* UART adapter only: the simulator, encoder, graphs, menus and help above
 * are the user's original HMI_F407_F103_Simulator(5).html. */
(() => {
  'use strict';
  const R = window.PchReference, {Sim, Encoder, Graph, RxTelemetry, DriveSettings} = R;
  const journalAdd=R.Journal.add;
  R.Journal.add=function(level,message){
    if(level==='info'&&/^(Панель:|Установлена модуляция:|Установлено вращение:|Установлено ограничение тока:|Изменён параметр:)/.test(message))return;
    return journalAdd.call(this,level,message);
  };
  Sim.dcLinkC = .003;
  Sim.prechargeR = 150;
  let activeDischarge = false, dischargePrevious = 0, requested = {page:0,flags:1,division:200};
  let online = false, busy = false, historyOffset=0, graphRevision=0;
  let sweep={page:-1,span:0,bucket:-1,channels:[]};
  const modeNames = ['power','speed','limit','dqD','dqQ'];
  const style = document.createElement('style');
  style.textContent = '#uart-tools{position:fixed;left:12px;top:12px;width:260px;z-index:90;background:#14242c;color:#edf1f2;border:1px solid #52636b;border-radius:8px;padding:12px;font:13px Arial}#uart-tools button,#uart-tools select,#uart-tools input{font:13px Arial;margin:5px 2px;padding:7px;background:#213640;color:#edf1f2;border:1px solid #52636b;border-radius:4px}#uart-tools p{margin:8px 0;line-height:1.4}#uart-tools label{display:block}#uart-tools small{color:#b3c0c7}#uart-tools.collapsed>*:not(h3){display:none}#uart-tools h3{margin:0;cursor:pointer;font-size:14px}#discharge-choice{position:absolute;left:calc(100% + 10px);top:0;width:180px;padding:8px;border:1px solid #52636b;border-radius:5px;background:#14242c;color:#edf1f2;font:12px Arial;white-space:normal}.debug-panel{position:relative}';
  document.head.appendChild(style);
  style.textContent += '#help-modal .help-dialog{height:414px;max-height:414px;overflow:hidden;display:flex;flex-direction:column}#help-content{flex:1;min-height:0;overflow:hidden}#help-modal .key-actions{flex-shrink:0;display:flex;gap:6px}#help-modal .key-actions button{flex:1}';
  const originalHelpOpen=window.Help.open;
  window.Help.open=function(){
    originalHelpOpen.call(this);
    if(!document.getElementById('help-modal').classList.contains('open'))return;
    const content=document.getElementById('help-content'),items=Array.from(content.children),groups=[[]];
    let height=0,page=0;
    for(const item of items){const h=item.offsetHeight+8;if(height&&height+h>content.clientHeight){groups.push([]);height=0;}groups[groups.length-1].push(item);height+=h;}
    const actions=document.querySelector('#help-modal .key-actions');
    actions.querySelectorAll('.help-page-button').forEach(e=>e.remove());
    const prev=document.createElement('button'),next=document.createElement('button');
    prev.className=next.className='help-page-button';prev.textContent='<';next.textContent='>';
    actions.prepend(prev);actions.append(next);
    function show(){items.forEach(item=>item.style.display=groups[page].includes(item)?'':'none');document.getElementById('help-title').textContent='СПРАВКА '+(page+1)+' / '+groups.length;}
    prev.onclick=()=>{if(page)page--;show();};next.onclick=()=>{if(page+1<groups.length)page++;show();};show();
  };
  style.textContent += '#uart-tools select{width:100%;min-width:0}#discharge-choice{left:calc(100% + 8px);width:132px;border:0;padding:4px;font-size:11px}.encoder-hint{padding-top:30px;font-size:8px}';
  const panel = document.createElement('aside');panel.id='uart-tools';
  panel.innerHTML='<h3>UART · терминал ПЧ ▾</h3><p>150 Ом · 3 мФ<br>Модель и управление из вашего HTML</p><select id="uart-port" aria-label="COM-порт"></select><button id="uart-refresh">Обновить</button><button id="uart-connect">Подключить</button><p id="uart-status">Локальная симуляция</p><label>Утечка, кОм <input id="uart-leak" type="number" min="0.1" max="100000" value="100" style="width:80px"></label><label>Разряд инвертором, Вт <input id="uart-power" type="number" min="0.1" max="1000" value="30" style="width:70px"></label><small>Энкодер: колесо/перетаскивание — черновик; нажатие — применить; удержание 0,5 с — следующий параметр.</small>';
  document.body.appendChild(panel);
  panel.querySelector('h3').onclick=()=>panel.classList.toggle('collapsed');
  const choice=document.createElement('label');choice.id='discharge-choice';
  choice.innerHTML='<input id="uart-active-discharge" type="checkbox" checked> Разряд через инвертор';
  document.querySelector('.debug-panel').appendChild(choice);
  const stateElement=document.getElementById('uart-status');
  async function api(path,data){
    const response=await fetch(path,data===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
    const result=await response.json();if(!response.ok)throw Error(result.error||response.statusText);return result;
  }
  async function ports(){try{const list=await api('/api/ports');document.getElementById('uart-port').replaceChildren(...list.map(p=>{const o=document.createElement('option');o.value=p.port;o.textContent=p.port+' · '+p.label;return o;}));}catch(e){stateElement.textContent=e.message;}}
  document.getElementById('uart-refresh').onclick=ports;
  document.getElementById('uart-connect').onclick=async()=>{try{await api(online?'/api/disconnect':'/api/connect',{port:document.getElementById('uart-port').value});online=!online;document.getElementById('uart-connect').textContent=online?'Отключить':'Подключить';}catch(e){stateElement.textContent=e.message;}};
  const network={vWarnLow:198,vWarnHigh:242,vFaultLow:180,vFaultHigh:260,fWarnLow:49,fWarnHigh:51,fFaultLow:45,fFaultHigh:55};
  function severity(value,low,high,faultLow,faultHigh){return value<faultLow||value>faultHigh?3:value<low||value>high?2:1;}
  function mainsStates(){return [severity(Sim.mainsVoltage,network.vWarnLow,network.vWarnHigh,network.vFaultLow,network.vFaultHigh),severity(Sim.mainsFrequency,network.fWarnLow,network.fWarnHigh,network.fFaultLow,network.fFaultHigh)];}
  const oldStep=Sim.step, oldPublish=Sim.publishTelemetry, oldToggle=Sim.toggleCharge;
  Sim.toggleInverter=function(){
    if(this.chargeState==='discharging'&&activeDischarge){
      this.chargeStartDc=this.dc;this.chargeStartedAt=performance.now();this.chargeElapsed=0;
      activeDischarge=false;Graph.pause();this.publishTelemetry();R.Journal.add('info','СТОП: продолжение самостоятельного разряда');return;
    }
    const wasRunning=this.inverterOn;
    if(!wasRunning&&(mainsStates().includes(3)||this.chargeState!=='charged'||this.dc<this.mainsVoltage*Math.SQRT2*.92)){R.toast('Пуск недоступен: проверьте сеть и заряд DC');return;}
    this.inverterOn=!wasRunning;this.publishTelemetry();this.updateUI();R.Journal.add('info',this.inverterOn?'Инвертор включён':'Инвертор выключен');
    if(wasRunning&&!this.inverterOn)Graph.pause();
    if(!wasRunning&&this.inverterOn){Graph.startNew();historyOffset=0;graphRevision++;sweep.page=-1;}
  };
  function dischargeVoltage(t){
    const start=Sim.chargeStartDc, leak=Math.max(100,Number(document.getElementById('uart-leak').value)*1000||100000);
    if(!activeDischarge)return start*Math.exp(-t/(leak*Sim.dcLinkC));
    const watts=Math.max(.1,Number(document.getElementById('uart-power').value)||30);
    return Math.sqrt(Math.max(0,start*start-2*watts*t/Sim.dcLinkC))*Math.exp(-t/(leak*Sim.dcLinkC));
  }
  Sim.step=function(dt){
    if(mainsStates().includes(3)&&(this.chargeState==='charging'||this.chargeState==='charged')){
      this.inverterOn=false;activeDischarge=false;Graph.pause();oldToggle.call(this);R.Journal.add('fault','Сеть: аварийное напряжение или частота');
    }
    if(this.chargeState!=='discharging'){oldStep.call(this,dt);
      const target=Math.max(0,this.mainsVoltage)*Math.SQRT2;
      if(this.chargeState==='charging')this.dc=target-Math.max(0,target-this.chargeStartDc)*Math.exp(-this.chargeElapsed/(this.prechargeR*this.dcLinkC));
      else if(this.chargeState==='charged')this.dc=target;
      return;
    }
    const elapsed=this.chargeElapsed;
    this.chargeState='discharged';oldStep.call(this,dt);this.chargeState='discharging';
    this.dc=dischargeVoltage(elapsed);
    if(this.dc<1){this.dc=0;this.chargeState='discharged';R.Journal.add('info','DC шина разряжена');}
  };
  Sim.toggleCharge=function(){
    const beginning=this.chargeState==='charging'||this.chargeState==='charged';
    if(!beginning&&mainsStates().includes(3)){R.toast('Авария сети: заряд недоступен');return;}
    if(beginning)activeDischarge=document.getElementById('uart-active-discharge').checked;
    oldToggle.call(this);dischargePrevious=0;
  };
  const originalSampler = (dt) => {
    if(Sim.chargeState==='charging'){
      const tau=Sim.prechargeR*Sim.dcLinkC,target=Math.max(0,Sim.mainsVoltage)*Math.SQRT2;
      return target-Math.max(0,target-Sim.chargeStartDc)*Math.exp(-Sim.chargeElapsed/tau);
    }
    return Sim.dc;
  };
  R.setGraphDcSampler(originalSampler);
  Sim.publishTelemetry=function(){
    oldPublish.call(this);
    const v=RxTelemetry.values,s=RxTelemetry.states;
    const discharge=this.chargeState==='discharging', disconnected=discharge||this.chargeState==='discharged';
    v.mainsVoltage=this.mainsVoltage;v.mainsFrequency=this.mainsFrequency;
    s.blocks.mains=s.indicators.mains=['inactive','normal','warning','fault'][Math.max(...mainsStates())];
    if(this.dc<1){s.indicators.precharge=s.indicators.dc='inactive';}
    if(this.inverterOn&&(s.controls.mod==='warning'||s.controls.freq==='warning'))s.blocks.motor='warning';
    if(discharge&&activeDischarge){
      s.blocks.inverter='process';s.switches.inverter='closed';
      const watts=Math.max(.1,Number(document.getElementById('uart-power').value)||30);
      v.outputVoltage=this.dc;v.outputCurrent=this.dc>1?watts/this.dc:0;v.outputPower=watts/1000;
    }
    document.getElementById('debug-charge').textContent=(this.chargeState==='charging'||this.chargeState==='charged')?'РАЗРЯД':'ЗАРЯД';
    document.getElementById('debug-inverter').textContent=(this.inverterOn||(discharge&&activeDischarge))?'СТОП':'ПУСК';
  };
  function fieldEvent(e){
    if(e.type===8){R.Auto.values=R.ParamData.motor.slice(0,8).map(row=>row[1]);R.Auto.start();return;}
    if(e.type===9){R.Auto.cancel();return;}
    if(e.type===10&&e.id===300){R.Auto.save();return;}
    if(e.type===1){
      if(e.id<3){const names=['mod','freq','limit'],key=names[e.id];Encoder.pending[key]=e.id===2?e.value/100:e.value;Sim.select(key);Encoder.apply();return;}
      const map={3:['system',0],4:['system',1],5:['system',2],6:['inverter',0],7:['inverter',1],8:['inverter',2],
        9:['motor',0],10:['motor',1],11:['motor',2],12:['motor',3],13:['motor',4],14:['motor',5],15:['motor',6],16:['motor',7],
        17:['protections',0],18:['protections',1],19:['protections',2],20:['protections',3],21:['protections',4],22:['protections',5],23:['protections',6],
        24:['communication',1],25:['communication',2],26:['communication',4],27:['calibration',0],28:['calibration',1],29:['calibration',2],30:['calibration',3],31:['calibration',4]};
      if(e.id>=48&&e.id<=55){network[["vWarnLow","vWarnHigh","vFaultLow","vFaultHigh","fWarnLow","fWarnHigh","fFaultLow","fFaultHigh"][e.id-48]]=e.value;}
      if(map[e.id]){const [section,row]=map[e.id];R.ParamData[section][row][1]=e.value;R.Params.render();}
    }else if(e.type===2&&e.id===10){DriveSettings.control=['uf','scalar','vector'][e.value]||'uf';Graph.refreshPresentation(true);}
    else if(e.type===10&&e.id===400)Sim.select(['mod','freq','limit'][e.value]||'mod');
    else if(e.type===3){
      if(e.id===0)Graph.setMode(modeNames[e.value]);
      if(e.id===1){Graph.duration=e.value*.005;Graph.saveView();Graph.draw();}
      if(e.id===2){if(e.value){Graph.startNew();historyOffset=0;sweep.page=-1;}else Graph.pause();graphRevision++;}
      if(e.id===3){const b=buffers[modeNames[requested.page]];historyOffset=Math.max(0,Math.min(Math.max(0,b.count-requested.division*.005*R.SAMPLE_RATE),historyOffset-e.value*requested.division*.0025*R.SAMPLE_RATE));graphRevision++;}
      if(e.id>=200&&e.id<=203){Graph.channelVisible[Graph.mode][e.id-200]=!!e.value;Graph.draw();}
    }
  }
  function snapshot(){
    const v=RxTelemetry.values,charging=Sim.chargeState==='charging',discharging=Sim.chargeState==='discharging',charged=Sim.chargeState==='charged';
    const state=mainsStates().includes(3)?5:charging?1:charged?(Sim.inverterOn?3:2):0;
    let flags=(Sim.inverterOn||(discharging&&activeDischarge)?1:0)|(Sim.mainsVoltage>=10?8:0)|(charged?16:0)|(discharging?(activeDischarge?64:32):0);
    if(Sim.inverterOn&&(RxTelemetry.states.controls.mod==='warning'||RxTelemetry.states.controls.freq==='warning'))flags|=2;else flags|=4;
    const actualI=v.currentLimitActual/R.nominalOutputCurrent()*100;
    const values={mains_voltage:v.mainsVoltage,mains_frequency:v.mainsFrequency,dc_bus_voltage:v.dcVoltage,precharge_current:v.prechargeCurrent,precharge_seconds:v.chargeTime,
      temperature_rectifier:v.tempRectifier,temperature_precharge:v.tempPrecharge,temperature_dc:v.tempDc,temperature_inverter:v.tempInverter,temperature_motor:v.tempMotor,
      modulation_set:v.modSet,modulation_actual:v.modActual,rotation_set:v.freqSet,rotation_actual:v.freqActual,current_limit_set:Sim.currentLimitPu*100,current_limit_actual:actualI,
      output_voltage:v.outputVoltage,output_current:v.outputCurrent,output_power:v.outputPower,rotor_frequency:v.rotorFrequency,stator_frequency:v.statorFrequency,slip:v.slipPct,motor_load:-1};
    const originalMode=Graph.mode,page=requested.page,mode=modeNames[page];Graph.mode=mode;
    const ranges=Graph.ranges(),buffer=R.buffers[mode],view=Graph.views[mode],end=Graph.collecting?buffer.total:(view.end||buffer.total),span=Math.max(10,Math.round(requested.division*.005*R.SAMPLE_RATE));
    const count=Graph.activeChannels().length,channels=[],scales=[];
    const running=(requested.flags&1)!==0;
    let cursor=0;
    if(running){
      const bucket=Math.floor(buffer.total*240/span);
      if(sweep.page!==page||sweep.span!==span||sweep.channels.length!==count||bucket<sweep.bucket){sweep={page,span,bucket:Math.max(-1,bucket-240),channels:Array.from({length:count},()=>Array(240).fill(0))};}
      for(let n=Math.max(sweep.bucket+1,bucket-239);n<=bucket;n++){
        const pos=239-((n%240+240)%240),index=Math.min(buffer.total-1,Math.floor(n*span/240));
        for(let ch=0;ch<count;ch++){const val=buffer.get(index,ch);sweep.channels[ch][pos]=Number.isFinite(val)?val:0;}
      }
      sweep.bucket=bucket;cursor=240-((bucket%240+240)%240);
    }
    for(let ch=0;ch<count;ch++){
      const unit=Graph.units()[ch],scale=unit===''?1000:unit==='А'?100:10;scales.push(scale);
      {const points=[];for(let i=0;i<240;i++){const index=end-1-(running?0:historyOffset)-span+Math.round(i*span/239),val=buffer.get(index,ch);points.push(Number.isFinite(val)?val:0);}channels.push(points);}
    }
    Graph.mode=originalMode;
    const warningMask=(RxTelemetry.states.controls.mod==='warning'?1:0)|(RxTelemetry.states.controls.freq==='warning'?2:0)|(RxTelemetry.states.controls.limit==='warning'?4:0);
    const pending=[Encoder.pending.mod,Encoder.pending.freq,Encoder.pending.limit*100];
    let pendingMask=0;Encoder.order.forEach((key,i)=>{if(Encoder.dirty(key))pendingMask|=1<<i;});
    const mains=mainsStates(),fault=mains.includes(3),on=Sim.chargeState==='charged'||Sim.chargeState==='charging';
    const status=on?1:0,transition=Sim.chargeState==='charging'||discharging?2:status;
    const visual=[...mains,status,transition,transition,Sim.inverterOn||discharging&&activeDischarge?1:0,Sim.inverterOn?(warningMask?2:1):0,...Array(5).fill(status)];
    if(fault)visual.fill(0,2);
    return {time:performance.now()/1000,state:fault?5:state,flags,visual,mode:['uf','scalar','vector'].indexOf(DriveSettings.control),selected:Encoder.order.indexOf(Sim.selected),values,pending,pendingMask,warningMask,
      auto:{progress:R.Auto.progress,active:R.Auto.running,done:R.Auto.done,values:[.84,.71,4.3,4.3,142]},graph:{page,offsetMs:running?0:Math.round(historyOffset*1000/R.SAMPLE_RATE),division:requested.division,channels,ranges,scales,cursor,revision:graphRevision}};
  }
  async function poll(){
    if(busy)return;busy=true;
    try{const response=await api('/api/exchange',snapshot());requested=response.graph;online=response.connected;stateElement.textContent=response.status;
      document.getElementById('uart-connect').textContent=online?'Отключить':'Подключить';response.events.forEach(fieldEvent);
    }catch(e){stateElement.textContent=e.message;}finally{busy=false;}
  }
  window.PchUart={network,snapshot,poll,fieldEvent,get requested(){return requested;},get activeDischarge(){return activeDischarge;}};
  ports();setInterval(poll,100);Sim.publishTelemetry();Sim.updateUI();
})();
