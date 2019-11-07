#!/usr/bin/env lua
-- Calculate summary statistics for a series of numbers

package.path = package.path .. ";utils/?.lua"

local M = require "mean"
local P = require "percentile"

local numbers = {}

for number in io.lines() do
	numbers[#numbers+1] = tonumber(number)
end

if #numbers == 0 then os.exit(1) end

local P0   = P(  0, numbers)
local P10  = P( 10, numbers)
local P25  = P( 25, numbers)
local P50  = P( 50, numbers)
local P75  = P( 75, numbers)
local P90  = P( 90, numbers)
local P100 = P(100, numbers)

print "Min,P10,P25,Median,P75,P90,Max,P75-P25,P90-P10,Max-Min,Mean ± RSD"
print(string.format(string.rep("%.3f,", 10) .. "%.3f ± %.2f %%",
	P0,				-- minimum
	P10,			-- 10th percentile
	P25,			-- 25th percentile / first quartile
	P50,			-- median / second quartile
	P75,			-- 75th percentile / third quartile
	P90,			-- 90th percentile
	P100,			-- maximum
	P75-P25,		-- interquartile range
	P90-P10,		-- central 80 % range
	P100-P0,		-- total range,
	M(numbers)		-- mean ± relative standard deviation in percent
))
