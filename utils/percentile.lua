#!/usr/bin/env lua
-- Calculate the p-th percentile of a series of numbers
-- 0 <= p <= 100

local function isint(n)
	return n == math.floor(n)
end

local function percentile(p, numbers)
	assert(p >= 0 and p <= 100, "0 <= p <= 100")
	table.sort(numbers)
	if p == 0 then return numbers[1] end
	if p == 100 then return numbers[#numbers] end
	local rank = p/100 * #numbers + 0.5

	-- Round rank (guaranteed to be > 0) to the nearest integer
	--return numbers[math.floor(rank + 0.5)]

	if rank < 1 then return numbers[1] end
	if rank > #numbers then return numbers[#numbers] end
	if isint(rank) then return numbers[rank] end
	local x, y = math.modf(rank)
	return (1-y) * numbers[x] + y * numbers[x+1]
end

-- If required rather than called directly
if ({...})[1] == "percentile" then return percentile end

if #arg ~= 1 then
	print(string.format("Usage: %s <p-th percentile>", arg[0]))
	os.exit()
end

local p = tonumber(arg[1])
local numbers = {}

for number in io.lines() do
	numbers[#numbers+1] = tonumber(number)
end

print(string.format("P%d = %.3f", p, percentile(p, numbers)))
