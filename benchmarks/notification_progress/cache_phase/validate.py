# SPDX-License-Identifier: Apache-2.0
"""Validate complete whole-phase cache counter exports (never API-exclusive)."""
CPU = ('rpc_cpu', 'idle_cpu', 'low_cpu', 'sample_cpu', 'stream_cpu')
def require(value, message):
    if not value:raise ValueError(message)
def u32(value):return type(value) is int and 0 <= value < 2**32

def probe_windows(rows, mode, screen=True):
    require(type(mode) is int and mode in (-1,0,1), 'unknown sampling mode')
    configs=[r['device'] for r in rows if r['event']=='cache_probe_config']
    if mode==-1:
        require(not configs and not any(r['event']=='cache_probe' for r in rows),'baseline private command')
        return []
    require(configs==[dict(mode=mode,scope='phase',filter_start=0x42000000,filter_end=0x43ffffff)],'probe mode/filter acknowledgment')
    windows=[];current=None
    for row in rows:
        if row['event'] in CPU:
            require(current is None,'missing probe page')
            current=row['event']
        elif row['event']=='cache_probe':
            require(current is not None,'unscoped/duplicate probe page')
            d=row['device']
            require(set(d)=={'page','epoch','mode','access','miss','done'},'probe fields')
            require(all(u32(d[k]) for k in d if k!='done'),'counter domain')
            require(d['page']==0 and d['epoch']==len(windows)+1 and d['mode']==mode and d['done'] is True,'epoch/page/mode/completion')
            require((d['access']>0 if mode else d['access']==d['miss']==0),'counter active/disabled condition')
            if mode==1 and current=='rpc_cpu':require(d['miss']>0,'miss counter sanity')
            windows.append(dict(cpu_event=current,**d));current=None
    require(current is None and len(windows)==sum(r['event'] in CPU for r in rows),'missing phase')
    if screen:
        require([w['cpu_event'] for w in windows]==['rpc_cpu','idle_cpu','low_cpu','sample_cpu']+['stream_cpu']*3,'phase order')
    return windows
