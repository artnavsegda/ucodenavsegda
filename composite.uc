import { lsdir, readfile } from 'fs';

let fileList = lsdir('./');

for (n in fileList) {
  print(n, "\n");
  print(readfile(n));
}