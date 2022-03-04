-- Brief: Helper functions made globally available to all Lua states.

local serpent = import('/lua/serpent.lua')

-- A settings preset for serpent.block(), for easy to-string of Lua objects.
function repr(obj: any): string
	return serpent.block(obj, {
		comment = false, numformat = '%.6g', indent = '\t', maxlevel = 2
	})
end
