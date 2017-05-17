/*
	uWScat - a "cat"-like program using uWebSockets (~a WebSocket telnet)

	TODO:
	-some comments
	-better description
	-make it close when remote host closes
	-use proper error codes in close
	-option to control 'auto new-line removal' (bConsumeNL) -> this makes us send zero-sized messages; desirable?
	-option to force sending text as BINARY
	-experiment with the uv_tty_t flag
	-add casting helpers

	ISSUES:
	-crashes when the remote hosts terminates in an unexpected way
*/

#include <cstdio>
#include <uv.h>
#include <uWS/uWS.h>
#include <cassert>
#include "scoped_exit.h"

#ifdef NDEBUG
#define LOG(FMT, ...) fprintf(stderr, (FMT "\n"), ##__VA_ARGS__)
#else
#define LOG(FMT, ...) fprintf(stderr, ("uWScat<%s> " FMT "\n"), __PRETTY_FUNCTION__, ##__VA_ARGS__)
#endif


namespace
{
	void CloseUV(uv_handle_t* p)
	{
		if(!uv_is_closing(p)) uv_close(p, nullptr);
	}
}

/*
	It is _SO_ wrong to allocate a 64K buffer on the heap EACH TIME we read from stdin,
	but this is the libuv way and I know it much to little to argue. Since this is a
	user-interactive program I'll let it be - even though I do feel super bad about it.
*/
void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	buf->base = (char*) malloc(suggested_size);
	buf->len = suggested_size;
}

void on_read_tty(uv_stream_t* pTTY, ssize_t nread, const uv_buf_t* pBuf)
{
	assert(pTTY->data && "TTY has no attached data");

	auto scopedBuffer = make_scoped([pBuf]{ if(pBuf && pBuf->base) free(pBuf->base); });
	uWS::WebSocket<uWS::CLIENT>* ws = reinterpret_cast<uWS::WebSocket<uWS::CLIENT>*>(pTTY->data);

	if(nread > 0)
	{
		const bool bConsumeNL = true;	//< TODO
		if(bConsumeNL)
		{
			if(pBuf->base[nread-1] == '\n') --nread;
		}
		ws->send(pBuf->base, nread, uWS::BINARY);	//< TODO
	}
	else if(nread < 0)
	{
		if(nread != UV_EOF) LOG("TTY read error [%s], disconnecting...", uv_err_name(nread));
		else LOG("EOF from TTY, disconnecting and exiting...");
		CloseUV(reinterpret_cast<uv_handle_t*>(pTTY));
		ws->close();
	}
}

int main(int argc, const char** argv)
{
	if(argc != 2)
	{
		printf("\nUSAGE:	%s <URL>\n"
				"EXAMPLE %s wss://echo.websocket.org\n\n", argv[0], argv[0]);
		return 1;
	}

	uWS::Hub h;
	uv_tty_t* pTTY = nullptr;
	auto scopedTTY = make_scoped([pTTY]{ if(pTTY) { uv_tty_reset_mode(); delete pTTY; } });

	h.onMessage(
	[](uWS::WebSocket<uWS::CLIENT> *ws, char* message, size_t length, uWS::OpCode opCode)
	{
		message[length] = '\0';	//< apparently there's more in message than just the payload so clip it
		printf("[%c:%zu]>%s\n", opCode==uWS::BINARY?'B':'T', length, message);
	});

    h.onConnection(
	[&](uWS::WebSocket<uWS::CLIENT> *ws, uWS::HttpRequest req)
	{
		LOG("Connected to [%s] - use Ctrl+D to disconnect", argv[1]);
		pTTY = new uv_tty_t;
		uv_tty_init(h.getLoop(), pTTY, 0, 1);
		pTTY->data = ws;
		uv_tty_set_mode(pTTY, UV_TTY_MODE_NORMAL);
		uv_read_start(reinterpret_cast<uv_stream_t*>(pTTY), alloc_buffer, on_read_tty);
    });

	h.onDisconnection(
	[&](uWS::WebSocket<uWS::CLIENT>* ws, int code, char* message, size_t length)
	{
		std::string strMsg;
		if(message && length) strMsg.assign(message, length);
		LOG("Disconnected (code=%d, msg='%s')", code, strMsg.empty() ? "<nullptr>" : strMsg.c_str());
		//uv_read_stop(reinterpret_cast<uv_stream_t*>(pTTY));
		CloseUV(reinterpret_cast<uv_handle_t*>(pTTY));
		//h.getDefaultGroup<uWS::CLIENT>().close();
		//! hmm, it appears we're hanging on read(stdin) even if we uv_close() it
	});

	h.onError(
	[&](void *user)
	{
        LOG("ERROR: Connection to [%s] failed! Timeout?\n", argv[1]);
    });

	h.connect(argv[1]);
	h.run();
	return 0;
}

