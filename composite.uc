import { lsdir } from 'fs';

let fileList = lsdir('./');

for (n in fileList) {
  print(n, "\n");
}