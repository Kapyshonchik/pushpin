/*
 * Copyright (C) 2012 Fan Out Networks, Inc.
 *
 * This file is part of Pushpin.
 *
 * Pushpin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Pushpin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "m2requestpacket.h"

#include <QSet>
#include <qjson/parser.h>
#include "tnetstring.h"

static bool isAllCaps(const QString &s)
{
	for(int n = 0; n < s.length(); ++n)
	{
		QChar c = s[n];
		if(!c.isUpper())
			return false;
	}

	return true;
}

static QString makeMixedCaseHeader(const QString &s)
{
	QString out;
	for(int n = 0; n < s.length(); ++n)
	{
		QChar c = s[n];
		if(n == 0 || (n - 1 >= 0 && s[n - 1] == '-'))
			out += c.toUpper();
		else
			out += c;
	}

	return out;
}

M2RequestPacket::M2RequestPacket() :
	uploadDone(false)
{
}

bool M2RequestPacket::fromByteArray(const QByteArray &in)
{
	int start = 0;
	int end = in.indexOf(' ');
	if(end == -1)
		return false;

	sender = in.mid(start, end - start);

	start = end + 1;
	end = in.indexOf(' ', start);
	if(end == -1)
		return false;

	id = in.mid(start, end - start);

	start = end + 1;
	end = in.indexOf(' ', start);
	if(end == -1)
		return false;

	QByteArray path_only = in.mid(start, end - start);

	start = end + 1;
	TnetString::Type type;
	int offset, size;
	if(!TnetString::check(in, start, &type, &offset, &size))
		return false;

	if(type != TnetString::Hash && type != TnetString::ByteArray)
		return false;

	bool ok;
	QVariant vheaders = TnetString::toVariant(in, start, type, offset, size, &ok);
	if(!ok)
		return false;

	QVariantMap headersMap;
	if(type == TnetString::Hash)
	{
		headersMap = vheaders.toMap();
	}
	else // ByteArray
	{
		QJson::Parser parser;
		vheaders = parser.parse(vheaders.toByteArray(), &ok);
		if(!ok)
			return false;

		headersMap = vheaders.toMap();
	}

	QMap<QString, QByteArray> m2headers;
	QMapIterator<QString, QVariant> vit(headersMap);
	while(vit.hasNext())
	{
		vit.next();

		if(vit.value().type() != QVariant::String)
			return false;

		m2headers[vit.key()] = vit.value().toString().toUtf8();
	}

	method = m2headers.value("METHOD");
	path = m2headers.value("URI");

	QByteArray uploadStartRaw = m2headers.value("x-mongrel2-upload-start");
	QByteArray uploadDoneRaw = m2headers.value("x-mongrel2-upload-done");
	if(!uploadDoneRaw.isEmpty())
	{
		// these headers must match for the packet to be valid. not
		//   sure why mongrel2 can't enforce this for us but whatever
		if(uploadStartRaw != uploadDoneRaw)
			return false;

		uploadFile = QString::fromUtf8(uploadDoneRaw);
		uploadDone = true;
	}
	else if(!uploadStartRaw.isEmpty())
	{
		uploadFile = QString::fromUtf8(uploadStartRaw);
	}

	QSet<QString> skipHeaders;
	skipHeaders += "x-mongrel2-upload-start";
	skipHeaders += "x-mongrel2-upload-done";

	headers.clear();
	QMapIterator<QString, QByteArray> it(m2headers);
	while(it.hasNext())
	{
		it.next();

		QString key = it.key();
		if(isAllCaps(key) || skipHeaders.contains(key))
			continue;

		headers += HttpHeader(makeMixedCaseHeader(key).toLatin1(), it.value());
	}

	start = offset + size + 1;
	if(!TnetString::check(in, start, &type, &offset, &size))
		return false;

	if(type != TnetString::ByteArray)
		return false;

	body = TnetString::toByteArray(in, start, offset, size, &ok);
	if(!ok)
		return false;

	return true;
}

#if 0
bool HttpRequestPacket::fromVariant(const QVariant &in)
{
	if(in.type() != QVariant::Hash)
		return false;

	// TODO
	QVariantHash obj = in.toHash();

	if(!obj.contains("id") || obj["id"].type() != QVariant::ByteArray)
		return false;
	id = obj["id"].toByteArray();

	if(!obj.contains("method") || obj["method"].type() != QVariant::ByteArray)
		return false;
	method = QString::fromLatin1(obj["method"].toByteArray());

	if(!obj.contains("url") || obj["url"].type() != QVariant::ByteArray)
		return false;
	url = QUrl::fromEncoded(obj["url"].toByteArray(), QUrl::StrictMode);

	if(obj.contains("headers"))
	{
		if(obj["headers"].type() != QVariant::List)
			return false;

		headers.clear();
		foreach(const QVariant &i, obj["headers"].toList())
		{
			QVariantList list = i.toList();
			if(list.count() != 2)
				return false;

			if(list[0].type() != QVariant::ByteArray || list[1].type() != QVariant::ByteArray)
				return false;

			headers += QPair<QByteArray, QByteArray>(list[0].toByteArray(), list[1].toByteArray());
		}
	}

	body.clear();
	if(obj.contains("body"))
	{
		if(obj["body"].type() != QVariant::ByteArray)
			return false;

		body = obj["body"].toByteArray();
	}

	stream = false;
	if(obj.contains("stream"))
	{
		if(obj["stream"].type() != QVariant::Bool)
			return false;

		stream = obj["stream"].toBool();
	}

	maxSize = -1;
	if(obj.contains("max-size"))
	{
		if(obj["max-size"].type() != QVariant::Int)
			return false;

		maxSize = obj["max-size"].toInt();
	}

	connectHost.clear();
	if(obj.contains("connect-host"))
	{
		if(obj["connect-host"].type() != QVariant::ByteArray)
			return false;

		connectHost = QString::fromUtf8(obj["connect-host"].toByteArray());
	}

	userData = obj["user-data"];

	return true;
}
#endif