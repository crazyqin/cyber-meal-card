// Local experiment: equivalent undetectable V8 object, without building an addon.
require('node:v8').setFlagsFromString('--allow-natives-syntax');
const factory=new Function('return %GetUndetectable()');
exports.getDocumentAll=function getDocumentAll(properties={}){
  const value=factory();
  for(const [key,item] of Object.entries(properties))value[key]=item;
  return value;
};
