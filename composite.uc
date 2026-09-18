import { stat, lsdir, readfile } from 'fs';

function dirLeaf(dir)
{
  let schema = {};
  let fileList = lsdir(dir);

  for (filename in fileList) {
    if (index(filename,'.json') != -1)
    {
      let filepath = dir + '/' + filename;
      if (stat(filepath).type == "file")
      {
        let schema = json(readfile(filepath));
        combinedSchema[filename] = schema;
      }
    }
  }
  return schema;
}

rootDir = '/usr/share/aurora/schema';
let combo = dirLeaf(rootDir);

