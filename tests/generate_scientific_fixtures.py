"""Small real-format fixtures with independent, analytically known values.
Run with Python containing numpy, h5py and netCDF4. HDF4 uses libmfhdf via ctypes.
Never uses GeoReader to generate expected values.
"""
from pathlib import Path
import argparse
import ctypes as C
import numpy as np
import h5py
from netCDF4 import Dataset

parser = argparse.ArgumentParser()
parser.add_argument('--hdf4-library', required=True)
args = parser.parse_args()
out = Path(__file__).parent / 'data'
out.mkdir(exist_ok=True)
base = (np.arange(4)[:, None, None]*100 + np.arange(4)[None, :, None]*10 + np.arange(5)[None, None, :]).astype('int16')
base[2, 1, 1] = -9999
for filename, fmt, axes in [('netcdf-classic.nc','NETCDF3_CLASSIC',('time','lat','lon')),('netcdf4.nc','NETCDF4',('time','lat','lon')),('permuted.nc','NETCDF4',('lat','time','lon')),('levels.nc','NETCDF4',('time','depth','lat','lon'))]:
    with Dataset(out / filename,'w',format=fmt) as ds:
        ds.Conventions='CF-1.8'
        for name,n,values in [('time',4,[0.,2.,5.,9.]),('lat',4,[40.,39.,38.,37.]),('lon',5,[100.,101.,102.,103.,104.])]:
            ds.createDimension(name,n)
            var=ds.createVariable(name,'f8',(name,));var[:]=values
            var.standard_name={'time':'time','lat':'latitude','lon':'longitude'}[name]
            var.units={'time':'days since 2020-01-01 00:00:00','lat':'degrees_north','lon':'degrees_east'}[name]
            var.axis={'time':'T','lat':'Y','lon':'X'}[name]
        if 'depth' in axes: ds.createDimension('depth',2)
        a=ds.createVariable('temperature','i2',axes,fill_value=-9999);a.units='K';a.long_name='Test temperature';a.scale_factor=.5;a.add_offset=10.;a.set_auto_maskandscale(False)
        if axes==('lat','time','lon'): a[:]=base.transpose(1,0,2)
        elif 'depth' in axes: a[:]=np.stack([base,base+1000],axis=1)
        else:a[:]=base
        b=ds.createVariable('quality','f4',('lat','lon'));b[:]=np.ones((4,5))
with h5py.File(out/'scientific.h5','w') as f:
    t=f.create_dataset('time',data=[0.,2.,5.,9.]);t.make_scale('time');t.attrs['units']=np.bytes_('days since 2020-01-01 00:00:00');t.attrs['standard_name']=np.bytes_('time')
    y=f.create_dataset('y',data=np.arange(4));y.make_scale('y')
    x=f.create_dataset('x',data=np.arange(5));x.make_scale('x')
    a=f.create_dataset('science/temperature',data=base);a.attrs['_FillValue']=np.int16(-9999);a.attrs['units']=np.bytes_('K');a.attrs['scale_factor']=.5;a.attrs['add_offset']=10.
    for i,d in enumerate((t,y,x)):a.dims[i].attach_scale(d)
    f.create_dataset('quality',data=np.ones((4,5)))
h=C.CDLL(args.hdf4_library)
i32=C.c_int32
h.SDstart.argtypes=[C.c_char_p,i32];h.SDstart.restype=i32
h.SDcreate.argtypes=[i32,C.c_char_p,i32,i32,C.POINTER(i32)];h.SDcreate.restype=i32
h.SDgetdimid.argtypes=[i32,i32];h.SDgetdimid.restype=i32
h.SDsetdimname.argtypes=[i32,C.c_char_p]
h.SDsetattr.argtypes=[i32,C.c_char_p,i32,i32,C.c_void_p]
h.SDsetfillvalue.argtypes=[i32,C.c_void_p]
h.SDwritedata.argtypes=[i32,C.POINTER(i32),C.POINTER(i32),C.POINTER(i32),C.c_void_p]
file=h.SDstart(str(out/'scientific.hdf').encode(),4);assert file>=0
for name,values,dims in [('temperature',base,('time','y','x')),('quality',np.ones((4,5),dtype='int16'),('y','x'))]:
    shape=(i32*values.ndim)(*values.shape);v=h.SDcreate(file,name.encode(),22,values.ndim,shape);assert v>=0
    for i,d in enumerate(dims):assert h.SDsetdimname(h.SDgetdimid(v,i),d.encode())==0
    fill=C.c_int16(-9999);assert h.SDsetfillvalue(v,C.byref(fill))==0
    start=(i32*values.ndim)(*([0]*values.ndim));assert h.SDwritedata(v,start,None,shape,np.ascontiguousarray(values).ctypes.data)==0
    assert h.SDendaccess(v)==0
assert h.SDend(file)==0
print('Created six scientific fixtures in',out)
