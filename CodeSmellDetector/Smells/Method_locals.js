const Util = require("../Utility.js");

module.exports = {
    callback: async function(ST, args){
        let def_count, msg, src, smell_level, report = [];
        for(const structure_id in ST.structures){
            const structure = ST.structures[structure_id];
            for(const method_id in structure.methods){
                const method = structure.methods[method_id];
                def_count = 0;
                Object.keys(method.definitions).length;
                for(const def_id in method.definitions){
                    const def = method.definitions[def_id];

                }

                smell_level = Util.get_smell_lvl(args.max_literals.min, args.max_literals.max, method.literals);
                if(smell_level > 0){
                    msg = `Method: "${method_id}" has ${method.literals} literals.`;
                    src = Util.get_src_obj(method.src_info.file, method.src_info.line, method.src_info.col, structure_id, method_id);
                    report.push(Util.get_incident_obj(src, msg, smell_level));
                }
            }
        }
        return report;
    }
}