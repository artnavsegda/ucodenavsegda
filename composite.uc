import { lsdir, readfile } from 'fs';

let fileList = lsdir('./');

for (filename in fileList) {
  if (index(filename,'.uc') != -1)
  {
    print(readfile(filename));
  }
}