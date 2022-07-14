#include "voipmonitor.h"

#include "hep.h"
#include "tools.h"
#include "header_packet.h"
#include "sniff_inline.h"
#include "sniff_proc_class.h"

#include <pcap.h>


static cHEP_Server *HEP_Server;


cHEP_ProcessData::cHEP_ProcessData() {
}

void cHEP_ProcessData::processData(u_char *data, size_t dataLen) {
	/*
	cout << " *** " << (isBeginHep(data, dataLen) ? "BEGIN" : "not begin") << endl;
	cout << " *** " << dataLen << endl;
	if(isBeginHep(data, dataLen)) {
		cout << " *** " << hepLength(data, dataLen) << endl;
		cout << " *** " << (isCompleteHep(data, dataLen) ? "COMPLETE" : "not complete") << endl;
	}
	*/
	if(isBeginHep(data, dataLen)) {
		hep_buffer.clear();
		if(isCompleteHep(data, dataLen)) {
			unsigned processed = processHeps(data, dataLen);
			if(processed < dataLen) {
				hep_buffer.add(data + processed, dataLen - processed);
			}
		}
	} else {
		hep_buffer.add(data, dataLen);
		if(isCompleteHep(hep_buffer.data(), hep_buffer.data_len())) {
			unsigned processed = processHeps(hep_buffer.data(), hep_buffer.data_len());
			if(processed == hep_buffer.data_len()) {
				hep_buffer.clear();
			} else if(processed < dataLen) {
				hep_buffer.removeDataFromLeft(processed);
			}
		}
	}
}

bool cHEP_ProcessData::isCompleteHep(u_char *data, size_t dataLen) {
	return(isBeginHep(data, dataLen) &&
	       hepLength(data, dataLen) <= dataLen);
}

bool cHEP_ProcessData::isBeginHep(u_char *data, size_t dataLen) {
	return(dataLen >= 4 &&
	       data[0] == 0x48 && data[1] == 0x45 && data[2] == 0x50 && data[3] == 0x33);
}

u_int16_t cHEP_ProcessData::hepLength(u_char *data, size_t dataLen) {
	return(dataLen >= 6 ? ntohs(*(u_int32_t*)(data + 4)) : 0);
}

unsigned cHEP_ProcessData::processHeps(u_char *data, size_t dataLen) {
	unsigned processed = 0;
	while(processed < dataLen - 6) {
		if(!isBeginHep(data + processed, dataLen - processed)) {
			break;
		}
		unsigned hep_length = hepLength(data + processed, dataLen - processed);
		if(hep_length > dataLen - processed) {
			break;
		}
		processHep(data + processed, hep_length);
		processed += hep_length;
	}
	return(processed);
}

void cHEP_ProcessData::processHep(u_char *data, size_t dataLen) {
	sHEP_Data hepData;
	processChunks(data+ 6, dataLen - 6, &hepData);
	if(sverb.hep3) {
		cout << " *** " << endl
		     << hepData.dump() << endl;
	}
	if((hepData.ip_protocol_family == PF_INET || hepData.ip_protocol_family == PF_INET6) &&
	   (hepData.ip_protocol_id == IPPROTO_UDP || hepData.ip_protocol_id == IPPROTO_TCP)) {
		extern int global_pcap_dlink;
		extern u_int16_t global_pcap_handle_index;
		extern int opt_id_sensor;
		extern PreProcessPacket *preProcessPacket[PreProcessPacket::ppt_end_base];
		ether_header header_eth;
		memset(&header_eth, 0, sizeof(header_eth));
		header_eth.ether_type = htons(ETHERTYPE_IP);
		if(hepData.ip_protocol_id == IPPROTO_TCP) {
			pcap_pkthdr *tcpHeader;
			u_char *tcpPacket;
			createSimpleTcpDataPacket(sizeof(header_eth), &tcpHeader,  &tcpPacket,
						  (u_char*)&header_eth, hepData.captured_packet_payload.data(), hepData.captured_packet_payload.data_len(), 0,
						  hepData.ip_source_address, hepData.ip_destination_address, hepData.protocol_source_port, hepData.protocol_destination_port,
						  0, 0, (hepData.set_flags & (1ull << _hep_chunk_tcp_flag)) ? hepData.tcp_flag : 0,
						  hepData.timestamp_seconds, hepData.timestamp_microseconds, global_pcap_dlink);
			unsigned iphdrSize = ((iphdr2*)(tcpPacket + sizeof(header_eth)))->get_hdr_size();
			unsigned dataOffset = sizeof(header_eth) + 
					      iphdrSize +
					      ((tcphdr2*)(tcpPacket + sizeof(header_eth) + iphdrSize))->doff * 4;
			packet_flags pflags;
			pflags.init();
			pflags.tcp = 2;
			sPacketInfoData pid;
			pid.clear();
			preProcessPacket[PreProcessPacket::ppt_detach]->push_packet(
				#if USE_PACKET_NUMBER
				0, 
				#endif
				hepData.ip_source_address, hepData.protocol_source_port, hepData.ip_destination_address, hepData.protocol_destination_port, 
				hepData.captured_packet_payload.data_len(), dataOffset,
				global_pcap_handle_index, tcpHeader, tcpPacket, true, 
				pflags, (iphdr2*)(tcpPacket + sizeof(header_eth)), (iphdr2*)(tcpPacket + sizeof(header_eth)),
				NULL, 0, global_pcap_dlink, opt_id_sensor, vmIP(), pid,
				false);
		} else if(hepData.ip_protocol_id == IPPROTO_UDP) {
			pcap_pkthdr *udpHeader;
			u_char *udpPacket;
			createSimpleUdpDataPacket(sizeof(header_eth), &udpHeader,  &udpPacket,
						  (u_char*)&header_eth, hepData.captured_packet_payload.data(), hepData.captured_packet_payload.data_len(), 0,
						  hepData.ip_source_address, hepData.ip_destination_address, hepData.protocol_source_port, hepData.protocol_destination_port,
						  hepData.timestamp_seconds, hepData.timestamp_microseconds);
			unsigned iphdrSize = ((iphdr2*)(udpPacket + sizeof(header_eth)))->get_hdr_size();
			unsigned dataOffset = sizeof(header_eth) + 
					      iphdrSize + 
					      sizeof(udphdr2);
			packet_flags pflags;
			pflags.init();
			sPacketInfoData pid;
			pid.clear();
			preProcessPacket[PreProcessPacket::ppt_detach]->push_packet(
				#if USE_PACKET_NUMBER
				0,
				#endif
				hepData.ip_source_address, hepData.protocol_source_port, hepData.ip_destination_address, hepData.protocol_destination_port, 
				hepData.captured_packet_payload.data_len(), dataOffset,
				global_pcap_handle_index, udpHeader, udpPacket, true, 
				pflags, (iphdr2*)(udpPacket + sizeof(header_eth)), (iphdr2*)(udpPacket + sizeof(header_eth)),
				NULL, 0, global_pcap_dlink, opt_id_sensor, vmIP(), pid,
				false);
		}
	}
}

u_int16_t cHEP_ProcessData::chunkVendor(u_char *data, size_t dataLen) {
	return(dataLen >= 2 ? ntohs(*(u_int32_t*)(data + 0)) : 0);
}

u_int16_t cHEP_ProcessData::chunkType(u_char *data, size_t dataLen) {
	return(dataLen >= 4 ? ntohs(*(u_int32_t*)(data + 2)) : 0);
}

u_int16_t cHEP_ProcessData::chunkLength(u_char *data, size_t dataLen) {
	return(dataLen >= 6 ? ntohs(*(u_int32_t*)(data + 4)) : 0);
}

void cHEP_ProcessData::processChunks(u_char *data, size_t dataLen, sHEP_Data *hepData) {
	unsigned offset = 0;
	while(offset < dataLen - 6) {
		unsigned chunk_length = chunkLength(data + offset, dataLen - offset);
		if(chunk_length > dataLen - offset) {
			break;
		}
		processChunk(data + offset, chunk_length, hepData);
		offset += chunk_length;
	}
}

void cHEP_ProcessData::processChunk(u_char *data, size_t dataLen, sHEP_Data *hepData) {
	unsigned chunk_type = chunkType(data, dataLen);
	unsigned payloadLen = dataLen - 6;
	u_char *payload = data + 6;
	bool ok = false;
	SimpleBuffer *bin = NULL;
	switch(chunk_type) {
	case _hep_chunk_ip_protocol_family:
		if(payloadLen == 1) {
			hepData->ip_protocol_family = *(u_int8_t*)payload;
			ok = true;
		}
		break;
	case _hep_chunk_ip_protocol_id:
		if(payloadLen == 1) {
			hepData->ip_protocol_id = *(u_int8_t*)payload;
			ok = true;
		}
		break;
	case _hep_chunk_ip_source_address_v4:
		if(payloadLen == 4) {
			hepData->ip_source_address.setIPv4(*(u_int32_t*)payload, true);
			ok = true;
		}
		break;
	case _hep_chunk_ip_source_address_v6:
		if(payloadLen == 16) {
			hepData->ip_source_address.setIPv6(*(in6_addr*)payload, true);
			ok = true;
		}
		break;
	case _hep_chunk_ip_destination_address_v4:
		if(payloadLen == 4) {
			hepData->ip_destination_address.setIPv4(*(u_int32_t*)payload, true);
			ok = true;
		}
		break;
	case _hep_chunk_ip_destination_address_v6:
		if(payloadLen == 16) {
			hepData->ip_destination_address.setIPv6(*(in6_addr*)payload, true);
			ok = true;
		}
		break;
	case _hep_chunk_protocol_source_port:
		if(payloadLen == 2) {
			hepData->protocol_source_port.setPort(*(u_int16_t*)payload, true);
			ok = true;
		}
		break;
	case _hep_chunk_protocol_destination_port:
		if(payloadLen == 2) {
			hepData->protocol_destination_port.setPort(*(u_int16_t*)payload, true);
			ok = true;
		}
		break;
	case _hep_chunk_timestamp_seconds:
		if(payloadLen == 4) {
			hepData->timestamp_seconds = ntohl(*(u_int32_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_timestamp_microseconds:
		if(payloadLen == 4) {
			hepData->timestamp_microseconds = ntohl(*(u_int32_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_protocol_type:
		if(payloadLen == 1) {
			hepData->protocol_type = *(u_int8_t*)payload;
			ok = true;
		}
		break;
	case _hep_chunk_capture_agent_id:
		if(payloadLen == 4) {
			hepData->capture_agent_id = ntohl(*(u_int32_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_keep_alive_timer:
		if(payloadLen == 2) {
			hepData->keep_alive_timer = ntohs(*(u_int16_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_authenticate_key:
		bin = &hepData->authenticate_key;
		break;
	case _hep_chunk_captured_packet_payload:
		hepData->captured_packet_payload.add(payload, payloadLen);
		ok = true;
		break;
	case _hep_chunk_captured_packet_payload_compressed:
		{
		cGzip gzipDecompress;
		if(gzipDecompress.isCompress(payload, payloadLen)) {
			u_char *dbuffer;
			size_t dbufferLength;
			if(gzipDecompress.decompress(payload, payloadLen, &dbuffer, &dbufferLength)) {
				hepData->captured_packet_payload.add(dbuffer, dbufferLength);
				delete [] dbuffer;
			}
		}
		}
		break;
	case _hep_chunk_internal_correlation_id:
		bin = &hepData->internal_correlation_id;
		break;
	case _hep_chunk_vlan_id:
		if(payloadLen == 2) {
			hepData->vlan_id = ntohs(*(u_int16_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_group_id:
		bin = &hepData->group_id;
		break;
	case _hep_chunk_source_mac:
		if(payloadLen == 8) {
			hepData->source_mac = be64toh(*(u_int64_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_destination_mac:
		if(payloadLen == 8) {
			hepData->destination_mac = be64toh(*(u_int64_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_ethernet_type:
		if(payloadLen == 2) {
			hepData->ethernet_type = ntohs(*(u_int16_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_tcp_flag:
		if(payloadLen == 1) {
			hepData->tcp_flag = *(u_int8_t*)payload;
			ok = true;
		}
		break;
	case _hep_chunk_ip_tos:
		if(payloadLen == 1) {
			hepData->ip_tos = *(u_int8_t*)payload;
			ok = true;
		}
		break;
	case _hep_chunk_mos_value:
		if(payloadLen == 2) {
			hepData->mos_value = ntohs(*(u_int16_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_r_factor:
		if(payloadLen == 2) {
			hepData->r_factor = ntohs(*(u_int16_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_geo_location:
		bin = &hepData->geo_location;
		break;
	case _hep_chunk_jitter:
		if(payloadLen == 4) {
			hepData->jitter = ntohl(*(u_int32_t*)payload);
			ok = true;
		}
		break;
	case _hep_chunk_transaction_type:
		bin = &hepData->transaction_type;
		break;
	case _hep_chunk_payload_json_keys:
		bin = &hepData->payload_json_keys;
		break;
	case _hep_chunk_tags_values:
		bin = &hepData->tags_values;
		break;
	case _hep_chunk_tag_type:
		if(payloadLen == 2) {
			hepData->tag_type = ntohs(*(u_int16_t*)payload);
			ok = true;
		}
		break;
	}
	if(bin) {
		bin->add(payload, payloadLen);
		ok = true;
	}
	if(ok) {
		hepData->set_flags |= (1ull << chunk_type);
	}
}


string sHEP_Data::dump() {
	ostringstream out;
	if(set_flags & (1ull << _hep_chunk_ip_protocol_family)) {
		out << "ip_protocol_family: " << (int)ip_protocol_family << endl;
	}
	if(set_flags & (1ull << _hep_chunk_ip_protocol_id)) {
		out << "ip_protocol_id: " << (int)ip_protocol_id << endl;
	}
	if(set_flags & (1ull << _hep_chunk_ip_source_address_v4) ||
	   set_flags & (1ull << _hep_chunk_ip_source_address_v6)) {
		out << "ip_source_address: " << ip_source_address.getString() << endl;
	}
	if(set_flags & (1ull << _hep_chunk_ip_destination_address_v4) ||
	   set_flags & (1ull << _hep_chunk_ip_destination_address_v6)) {
		out << "ip_destination_address: " << ip_destination_address.getString() << endl;
	}
	if(set_flags & (1ull << _hep_chunk_protocol_source_port)) {
		out << "protocol_source_port: " << protocol_source_port.getString() << endl;
	}
	if(set_flags & (1ull << _hep_chunk_protocol_destination_port)) {
		out << "protocol_destination_port: " << protocol_destination_port.getString() << endl;
	}
	if(set_flags & (1ull << _hep_chunk_timestamp_seconds)) {
		out << "timestamp_seconds: " << timestamp_seconds << endl;
	}
	if(set_flags & (1ull << _hep_chunk_timestamp_microseconds)) {
		out << "timestamp_microseconds: " << timestamp_microseconds << endl;
	}
	if(set_flags & (1ull << _hep_chunk_protocol_type)) {
		out << "protocol_type: " << (int)protocol_type << endl;
	}
	if(set_flags & (1ull << _hep_chunk_capture_agent_id)) {
		out << "capture_agent_id: " << capture_agent_id << endl;
	}
	if(set_flags & (1ull << _hep_chunk_keep_alive_timer)) {
		out << "keep_alive_timer: " << keep_alive_timer << endl;
	}
	if(set_flags & (1ull << _hep_chunk_authenticate_key)) {
		out << "authenticate_key: " << hexdump_to_string(&authenticate_key) << endl;
	}
	if(set_flags & (1ull << _hep_chunk_captured_packet_payload) ||
	   set_flags & (1ull << _hep_chunk_captured_packet_payload_compressed)) {
		string dump_payload((char*)captured_packet_payload);
		find_and_replace(dump_payload, "\r", "\\r");
		find_and_replace(dump_payload, "\n", "\\n");
		out << "captured_packet_payload: " << dump_payload << endl;
	}
	if(set_flags & (1ull << _hep_chunk_internal_correlation_id)) {
		out << "internal_correlation_id: " << hexdump_to_string(&internal_correlation_id) << endl;
	}
	if(set_flags & (1ull << _hep_chunk_vlan_id)) {
		out << "vlan_id: " << vlan_id << endl;
	}
	if(set_flags & (1ull << _hep_chunk_group_id)) {
		out << "group_id: " << hexdump_to_string(&group_id) << endl;
	}
	if(set_flags & (1ull << _hep_chunk_source_mac)) {
		out << "source_mac: " << hex << source_mac << dec << endl;
	}
	if(set_flags & (1ull << _hep_chunk_destination_mac)) {
		out << "destination_mac: " << hex << destination_mac << dec << endl;
	}
	if(set_flags & (1ull << _hep_chunk_ethernet_type)) {
		out << "ethernet_type: " << ethernet_type << endl;
	}
	if(set_flags & (1ull << _hep_chunk_tcp_flag)) {
		out << "tcp_flag: " << hex << (int)tcp_flag << dec << endl;
	}
	if(set_flags & (1ull << _hep_chunk_ip_tos)) {
		out << "ip_tos: " << (int)ip_tos << endl;
	}
	if(set_flags & (1ull << _hep_chunk_mos_value)) {
		out << "mos_value: " << mos_value << endl;
	}
	if(set_flags & (1ull << _hep_chunk_r_factor)) {
		out << "r_factor: " << r_factor << endl;
	}
	if(set_flags & (1ull << _hep_chunk_geo_location)) {
		out << "geo_location: " << hexdump_to_string(&geo_location) << endl;
	}
	if(set_flags & (1ull << _hep_chunk_jitter)) {
		out << "jitter: " << jitter << endl;
	}
	if(set_flags & (1ull << _hep_chunk_transaction_type)) {
		out << "transaction_type: " << hexdump_to_string(&transaction_type) << endl;
	}
	if(set_flags & (1ull << _hep_chunk_payload_json_keys)) {
		out << "payload_json_keys: " << hexdump_to_string(&payload_json_keys) << endl;
	}
	if(set_flags & (1ull << _hep_chunk_tags_values)) {
		out << "tags_values: " << hexdump_to_string(&tags_values) << endl;
	}
	if(set_flags & (1ull << _hep_chunk_tag_type)) {
		out << "tag_type: " << tag_type << endl;
	}
	return(out.str());
}


cHEP_Server::cHEP_Server(bool udp) 
 : cServer(udp, true) {
}

cHEP_Server::~cHEP_Server() {
}

void cHEP_Server::evData(u_char *data, size_t dataLen) {
	processData(data, dataLen);
}


cHEP_Connection::cHEP_Connection(cSocket *socket, bool simple_read) 
 : cServerConnection(socket, simple_read) {
}

cHEP_Connection::~cHEP_Connection() {
}

void cHEP_Connection::evData(u_char *data, size_t dataLen) {
	processData(data, dataLen);
}


void HEP_ServerStart(const char *host, int port, bool udp) {
	if(HEP_Server) {
		delete HEP_Server;
	}
	HEP_Server =  new FILE_LINE(0) cHEP_Server(udp);
	HEP_Server->setStartVerbString("START HEP LISTEN");
	HEP_Server->listen_start("hep_server", host, port);
}

void HEP_ServerStop() {
	if(HEP_Server) {
		delete HEP_Server;
		HEP_Server = NULL;
	}
}
