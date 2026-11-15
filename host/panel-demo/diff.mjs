// the wasm build through the same script as diff_native.c
import {readFileSync as R} from 'fs';
const m=(await WebAssembly.instantiate(R(process.argv[2]),{})).instance.exports;
const u8=()=>new Uint8Array(m.memory.buffer);
const str=p=>{const b=u8();let e=p;while(b[e])e++;return Buffer.from(b.subarray(p,e)).toString()};
const hash=()=>{const b=u8(),p=m.sim_fb();let h=2166136261;for(let i=0;i<8*132;i++){h^=b[p+i];h=Math.imul(h,16777619)>>>0}return h.toString(16).padStart(8,'0')};
m.sim_card_reset();let ms=1000;
for(const s of JSON.parse(R(process.argv[3]))){
  if(s.op=='P')m.sim_power(s.arg,ms);else if(s.op=='T')m.sim_turn(s.arg,ms);else if(s.op=='K')m.sim_card(s.arg,ms);else if(s.op=='R')m.sim_rom(s.arg);else if(s.op=='F')m.sim_font_file(s.arg);
  else if(s.op=='W'){for(let i=0;i<s.arg;i+=10){ms+=10;m.sim_tick(ms,s.pushed)}}
  ms+=10;m.sim_tick(ms,s.pushed);
  console.log(`${s.op} ${s.arg} ${hash()} ${m.sim_online()} ${m.sim_screen()} ${str(m.sim_mounted())}|${str(m.sim_dir())}`);
}
