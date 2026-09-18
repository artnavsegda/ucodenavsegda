import { lsdir, readfile } from 'fs';

dir = '/usr/share/aurora/schema';
let fileList = lsdir(dir);

for (filename in fileList) {
  print(filename);
  if (index(filename,'.json') != -1)
  {
    print(readfile(dir + '/' + filename));
  }
}