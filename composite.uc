import { stat, lsdir, readfile } from 'fs';

function dirLeaf(dir)
{
  let combinedSchema = {};
  let fileList = lsdir(dir);

  for (filename in fileList) {
    let filepath = dir + '/' + filename;
    if (index(filename,'.json') != -1)
    {
      if (stat(filepath)?.type == "file")
      {
        let schema = json(readfile(filepath));
        let schemaName = replace(filename,".json", "");
        combinedSchema[schemaName] = schema;
      }
    }
    else if (stat(filepath)?.type == "directory") 
    {
        combinedSchema[filename] = dirLeaf(filepath);
    }
  }
  return combinedSchema;
}

let rootDir = '/usr/share/aurora/schema';
let combo = dirLeaf(rootDir);
let str = sprintf("%J", combo);
print(str, "\n");