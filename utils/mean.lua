#!/usr/bin/env lua
-- Calculate mean and relative standard deviation (RSD) of a series of numbers

local function mean_rsd(numbers)
	if #numbers == 1 then return numbers[1], 0 end
	local sum = 0
	for i = 1, #numbers do
		sum = sum + numbers[i]
	end
	local mean = sum / #numbers
	sum = 0
	for i = 1, #numbers do
		sum = sum + (numbers[i] - mean)^2
	end
	local sd = math.sqrt(sum / (#numbers - 1))
	local rsd = 100 * sd / math.abs(mean)
	return mean, rsd
end

-- If required rather than called directly
if ({...})[1] == "mean" then return mean_rsd end

local numbers = {}

for number in io.lines() do
	numbers[#numbers+1] = tonumber(number)
end

print(string.format("%.2f ± %.2f %%", mean_rsd(numbers)))
