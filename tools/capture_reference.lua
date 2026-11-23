-- MAME 0.264+. Local diagnostics only. PACLAND_ROUND is a zero-based round.
-- Overrides the round index during startup; this is a diagnostic state injection.
local root=assert(os.getenv('PACLAND_CAPTURE_ROOT'),'PACLAND_CAPTURE_ROOT is required')
local round=tonumber(os.getenv('PACLAND_ROUND') or '0')
local frames=tonumber(os.getenv('PACLAND_FRAMES') or '720')
local frame=0
local scroll={0,0}
local bank=0
local mem=manager.machine.devices[':maincpu'].spaces['program']
local taps={}
taps[1]=mem:install_write_tap(0x3800,0x3bff,'capture_scroll',function(offset,data,mask)
 local layer=offset<0x3a00 and 1 or 2
 scroll[layer]=data+((offset&1)*256)
end)
taps[2]=mem:install_write_tap(0x3c00,0x3dff,'capture_bank',function(offset,data,mask) bank=data end)
taps[3]=mem:install_write_tap(0x2101,0x2101,'capture_round',function(offset,data,mask)
 if frame<500 then return round end
end)
local trace=assert(io.open(root..'/trace.csv','w'))
trace:write('frame,round,x,y,vx,vy,fg_scroll,bg_scroll,bank,state\n')
local function field(port,name,v) manager.machine.ioport.ports[port].fields[name]:set_value(v) end
local function signed(v) return v>=32768 and v-65536 or v end
local function dump()
 manager.machine.screens[':screen']:snapshot(root..'/arcade.png')
 local b=assert(io.open(root..'/ram.bin','wb'))
 for a=0,0x37ff do b:write(string.char(mem:read_u8(a))) end b:close()
 local info=assert(io.open(root..'/registers.json','w'))
 info:write(string.format('{"round_index":%d,"fg_scroll":%d,"bg_scroll":%d,"palette_bank":%d,"frame":%d}',mem:read_u8(0x2101),scroll[1],scroll[2],(bank>>3)&3,frame));info:close()
end
emu.register_frame_done(function()
 assert(taps[1]) -- retain tap handles for the entire run (Lua GC removes them).
 frame=frame+1
 if frame==180 then field(':IN0','Coin 1',1) end
 if frame==184 then field(':IN0','Coin 1',0) end
 if frame==210 then field(':IN0','1 Player Start',1) end
 if frame==214 then field(':IN0','1 Player Start',0) end
 if frame>=290 and frame<=304 then mem:write_u8(0x2236,round) end
 if frame==300 then field(':IN2','P1 Jump',1) end
 if frame==304 then field(':IN2','P1 Jump',0) end
 if frame>=420 then
  trace:write(string.format('%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n',frame,mem:read_u8(0x2101),mem:read_u16(0x240c),mem:read_u16(0x2409),signed(mem:read_u16(0x2411)),signed(mem:read_u16(0x240f)),scroll[1],scroll[2],bank,mem:read_u8(0x2403)))
 end
 if frame==frames then dump();trace:close();manager.machine:exit() end
end)
