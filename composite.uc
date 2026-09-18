import { stat, lsdir, readfile } from 'fs';

dir = '/usr/share/aurora/schema';
let fileList = lsdir(dir);

for (filename in fileList) {
  if (index(filename,'.json') != -1)
  {
    let filepath = dir + '/' + filename;
    if (stat(filepath).type == "file")
    {
      let schema = json(readfile(filepath));
    }
  }
}