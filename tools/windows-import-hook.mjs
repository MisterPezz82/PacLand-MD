// romdevtools 0.155.0 passes absolute Windows paths to dynamic import().
// Apply in the server AND inherited toolchain workers, without editing npm files.
import {registerHooks} from 'node:module';
import {pathToFileURL} from 'node:url';
registerHooks({resolve(specifier,context,nextResolve){
 return nextResolve(/^[A-Za-z]:[\\/]/.test(specifier)?pathToFileURL(specifier).href:specifier,context);
}});
