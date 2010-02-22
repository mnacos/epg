%%%--------------------------------------------------------------------- 
%%% epg -- an Erlang PostgreSQL libpq wrapper
%%% 
%%%   please read the associated LICENSE file
%%%   this code relies on the existence of an epg executable (epg.c)
%%%---------------------------------------------------------------------

-module(epg).
-export([start/0,start/1,stop/0,stop/1]).
-export([exec/1,exec/2,pp/1]).

% revision information
% ----------------------------------------------------------------------
-revision('Revision: 0.01 ').
-created('Date: 2009/11/24 11:32:01 ').
-created_by('m.nacos@gmail.com').
% ----------------------------------------------------------------------

start() ->
    spawn(fun() ->
		  register(epg, self()),
		  process_flag(trap_exit, true),
		  Port = open_port({spawn, "epg"}, [use_stdio, {line, 4096}]),
		  loop(Port)
	  end).

start(Id) ->
    spawn(fun() ->
		  register(Id, self()),
		  process_flag(trap_exit, true),
		  Port = open_port({spawn, "epg "++atom_to_list(Id)}, [use_stdio, {line, 4096}]),
		  loop(Port)
	  end).

stop() ->
    epg ! stop.

stop(Id) ->
    Id ! stop.

exec(Msg) ->
    epg ! {call, self(), Msg},
    receive
	{epg, Result} ->
	    Result
    end.

exec(Id,Msg) ->
    Id ! {call, self(), Msg},
    receive
	{epg, Result} ->
	    Result
    end.

% what we need here is some state... if not connected call must complain!
loop(Port) ->
    receive
		{call, Caller, Msg} ->
			port_command(Port, [Msg | "\n"]),
    		case collect_response(Port) of
        		{data, Response} -> 
            		Caller ! {epg, response_to_term(Response)};
        		{Atom, Response} -> 
					Splat = lists:flatten(lists:map(fun(X)->[X|"\n"] end, Response)),
            		Caller ! {epg, {Atom, Splat}};
        		timeout -> 
            		Caller ! {epg, timeout}
    		end,
	    	loop(Port);
		stop ->
			port_command(Port, ["quit\n"]),
    		case collect_response(Port) of
        		{bye, _} -> exit(normal);
				Unknown -> exit({bad_response, Unknown})
	    	end;
		{'EXIT', Port, Reason} ->
	    	exit({port_terminated,Reason})
    end.

collect_response(Port) ->
    collect_response(Port, [], []).

collect_response(Port, RespAcc, LineAcc) ->
    receive
        {Port, {data, {eol, "!eod!"}}} ->
            {data, lists:concat(lists:reverse(RespAcc))};

        {Port, {data, {eol, "!error!"}}} ->
            {error, lists:concat(lists:reverse(RespAcc))};

        {Port, {data, {eol, "!connected!"}}} ->
            {info, lists:concat(lists:reverse(RespAcc))};

        {Port, {data, {eol, "!bye!"}}} ->
            {bye, lists:concat(lists:reverse(RespAcc))};

        {Port, {data, {eol, Result}}} ->
            Line = lists:reverse([Result | LineAcc]),
            collect_response(Port, [ [Line | "\n"] | RespAcc], []);

        {Port, {data, {noeol, Result}}} ->
            collect_response(Port, RespAcc, [Result | LineAcc])

    %% Prevent the gen_server from hanging indefinitely in case the
    %% spawned process is taking too long processing the request.
    	after 72000000 -> 
           	timeout
    end.

get_timeout() ->
    {ok, Value} = application:get_env(epg, timeout),
    Value.

response_to_term(Response) ->
	Splat1 = re:replace(Response,"\n","",[multiline, global, {return, list}]),
	{ok, T, _} = erl_scan:string(Splat1++"."),
	{ok, Term} = erl_parse:parse_term(T),
	Term.

% pretty printing function for returned data
pp(Splat) ->
	case Splat of 
		[{header,Header},{data,Data}] ->
			Hlist = lists:map(fun({Item,_})->Item end, Header),
			io:format("~s~n",[pp(length(Hlist),Hlist,Data,[],[])]);
		Splat -> io:format("~p~n",[Splat])
	end.

% finding the maximum character width for each column
pp(_,[],Data,Header,L) ->
	pp(Data,lists:reverse(Header),lists:reverse(L));
pp(Total,Hlist,Data,Header,L) ->
	Pos = Total - length(Hlist) + 1,
	Col = [length(lists:nth(Pos,tuple_to_list(Tuple))) || Tuple <- Data],
	Max = lists:max(Col),
	[H|T] = Hlist,
	pp(Total,T,Data,[H|Header],[Max|L]).

% on to pretty printing in tabular form
pp(Data,Header,L) ->
	HString = [lists:concat([" ",string:centre(lists:nth(Num, Header),lists:nth(Num, L)+2)]) || Num <- lists:seq(1,length(Header),1)],
	Separator = lists:concat([lists:concat(["+", ["-" || _ <- lists:seq(1,Len+2,1)]]) || Len <- L]),
	% io:format("~s~n",[Separator]),
	Lines = [
				[
					lists:concat([
						"| ",
						string:left(
							lists:nth(Num,tuple_to_list(Tuple)),lists:nth(Num,L)+1
						)
					])
					|| Num <- lists:seq(1, length(tuple_to_list(Tuple)), 1)
				] || Tuple <- Data
	],
	Output = lists:concat([ [ lists:concat([Line, "\n"]) || Line <- Lines ] ]),
	lists:flatten([HString, "\n", Separator, "\n", Output]).

